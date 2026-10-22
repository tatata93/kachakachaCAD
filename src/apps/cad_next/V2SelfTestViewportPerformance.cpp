#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2GridDock.h"
#include "V2OperationPanelHost.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/modeling/GridModel.h"
#include "kachakacha/app/OriginPlanes.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QTimer>
#include <QWidget>
#include <QPixmap>
#include <QVariant>
#include <QCheckBox>
#include <QColor>
#include <QEvent>
#include <QIODevice>
#include <QPointF>
#include <QToolButton>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QComboBox>
#include <QObject>
#include <QDockWidget>
#include <QScrollArea>
#include <QPushButton>
#include <QChar>
#include <QString>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {
QImage Frame(V2Viewport& view) {
    QImage result(view.size(),QImage::Format_ARGB32);result.fill(Qt::transparent);view.render(&result);
    const auto path=qEnvironmentVariable("KACHACAD_GRID_PROOF_DIR");
    if(!path.isEmpty()){static int number=0;QDir dir(path);dir.mkpath(".");result.save(dir.filePath(QString("frame-%1.png").arg(++number)));}
    return result;
}
bool Near(const QImage& image,QPointF at,QColor color,int radius) {
    for(int y=std::max(0,int(at.y())-radius);y<std::min(image.height(),int(at.y())+radius+1);++y)
        for(int x=std::max(0,int(at.x())-radius);x<std::min(image.width(),int(at.x())+radius+1);++x) {
            const auto p=image.pixelColor(x,y);
            if(std::abs(p.red()-color.red())+std::abs(p.green()-color.green())+std::abs(p.blue()-color.blue())<25)return true;
        }
    return false;
}
void Shot(V2MainWindow& window,const QString& name) {
    const auto path=qEnvironmentVariable("KACHACAD_GRID_PROOF_DIR");if(path.isEmpty())return;
    QDir dir(path);dir.mkpath(".");window.grab().save(dir.filePath(name+".png"));
}
}
bool GridVisibilityInteraction(V2MainWindow& window) {
    window.RunCommand("file.new");auto& view=window.Viewport();view.SetViewDirection(ViewDirection::Top);
    view.SetVisibleWidthMm(200);
    auto* button=window.findChild<QToolButton*>("quickGridVisible");
    auto* check=window.findChild<QCheckBox*>("gridVisibility");if(!button||!check)return false;
    const auto revision=window.Session().GetDocument().Revision();
    auto choice=window.CurrentGridChoice();choice.grid.visible=true;choice.showInAllModes=true;window.ApplyGridChoice(choice);
    const auto point=view.Mapping().Project(view.WorkPlane().PointAt(20,30));if(!point)return false;
    const auto before=window.Session().PeekHover(*point).snap;
    if(!Explain("grid supplies a snap before hiding",before.has_value()))return false;
    const auto on=Frame(view);button->click();const auto off=Frame(view);
    const auto after=window.Session().PeekHover(*point).snap;
    if(!Explain("hiding grid only changes pixels, keeping snapping and toolbar/dock in sync",on!=off
        &&!view.DisplaySettingsNow().gridVisible&&!check->isChecked()
        &&window.Session().Scene().grid.visible&&after&&before->kind==after->kind&&before->position==after->position))return false;
    Shot(window,"grid-off");check->click();
    if(!Explain("dock shows grid through same display-only state",button->isChecked()&&view.DisplaySettingsNow().gridVisible))return false;
    for(const auto stage:{app::DisplayStage::NoGrid,app::DisplayStage::All}) {
        window.ApplyDisplayStage(stage);
        if(button->isChecked()!=view.DisplaySettingsNow().gridVisible||check->isChecked()!=button->isChecked())return false;
    }
    choice=window.CurrentGridChoice();choice.showInAllModes=false;window.ApplyGridChoice(choice);window.SetMode(app::UiMode::Part);
    if(!view.GridSuppressedByMode())return false;
    if(button->isChecked())return false;
    button->click();
    if(!Explain("explicit show works outside drawing mode",!view.GridSuppressedByMode()&&view.DisplaySettingsNow().gridInAllModes))return false;
    window.SetMode(app::UiMode::Drawing);window.SelectTool(modeling::DrawingTool::Line);
    const auto start=view.Mapping().Project({-30,-30,0});if(!start)return false;view.ClickAt({start->x,start->y});
    const auto placed=window.Session().PlacedPointCount();button->click();button->click();
    if(!Explain("grid switch keeps unfinished drawing and undo history",placed==1&&window.Session().PlacedPointCount()==placed
        &&window.Session().GetDocument().Revision()==revision))return false;
    window.SelectTool(modeling::DrawingTool::Select);Shot(window,"grid-on");return true;
}
bool PendingActionsFit(V2MainWindow& window) {
    window.resize(1400,900);QApplication::processEvents();
    window.RunCommand("file.new");
    const auto a=DrawRectangleAtByHand(window,.1,.5,.25,.65);
    const auto b=DrawRectangleAtByHand(window,.3,.5,.45,.65);
    if(a.IsNil()||b.IsNil())return false;
    const std::string name=QString(180,QChar(0x7a93)).toStdString();
    for(const auto id:{a,b})if(!window.Session().GetDocument().Run(document::RenameEntityCommand(id,name)).committed)return false;
    window.Viewport().SetSelection({});window.RunCommand("wire.join");
    window.Viewport().SetSelection(app::SelectionSet{{a,b}});QApplication::processEvents();
    auto* page=window.OperationHost().TemporaryPage();if(!page)return false;
    auto* confirm=page->findChild<QPushButton*>("pendingTargetConfirm");
    auto* cancel=page->findChild<QPushButton*>("pendingTargetCancel");
    for(const int height:{700,600}) {
        window.resize(1000,height);QApplication::processEvents();
        for(auto* button:{confirm,cancel})if(!Explain("long multiple target names leave fixed actions inside the operation panel",button
            &&button->isVisible()&&page->rect().contains(button->mapTo(page,button->rect().topLeft()))
            &&page->rect().contains(button->mapTo(page,button->rect().bottomRight()))))return false;
    }
    if(!confirm->isEnabled())return false;
    cancel->setFocus();const QPointF at=cancel->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress,at,cancel->mapToGlobal(at.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,at,cancel->mapToGlobal(at.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(cancel,&press);QApplication::sendEvent(cancel,&release);QApplication::processEvents();
    return Explain("pending target cancel clears inputs and leaves the tool ready to retry",window.Viewport().Selection().entityIds.empty()
        &&window.OperationHost().HasTemporaryPage()&&!confirm->isEnabled());
}
bool PersistentGridControls(V2MainWindow& window) {
    window.RunCommand("file.new");
    auto* dock=dynamic_cast<V2GridDock*>(window.findChild<QDockWidget*>("gridDock"));
    auto* spacing=window.findChild<QLineEdit*>("gridSpacing");
    auto* details=window.findChild<QToolButton*>("gridDetails");
    auto* body=window.findChild<QWidget*>("gridDetailsBody");
    if(!dock||!spacing||!details||!body)return false;
    window.resize(1000,700);QApplication::processEvents();
    if(!Explain("permanent grid and tool panel fit a 700px-high window",window.height()<=700))return false;
    for(const auto mode:app::AllUiModes()) {
        window.SetMode(mode);QApplication::processEvents();
        if(!Explain("grid settings persist in every mode",!dock->isHidden()&&!spacing->isHidden()&&dock->isAncestorOf(spacing)))return false;
        auto* scroll=qobject_cast<QScrollArea*>(dock->widget());
        if(scroll)std::cout<<"grid common dock="<<dock->height()<<" available="<<scroll->viewport()->height()
            <<" details bottom="<<details->mapTo(scroll->viewport(),details->rect().bottomRight()).y()<<std::endl;
        if(!Explain("frequent grid fields and details toggle are visible without scrolling",scroll
            &&scroll->viewport()->rect().contains(details->mapTo(scroll->viewport(),details->rect().bottomRight()))))return false;
    }
    window.SetMode(app::UiMode::Drawing);window.SelectTool(modeling::DrawingTool::Line);
    auto& view=window.Viewport();view.SetViewDirection(ViewDirection::Top);view.SetVisibleWidthMm(100);
    const auto start=view.Mapping().Project({-30,-30,0});if(!start)return false;view.ClickAt({start->x,start->y});
    const auto revision=window.Session().GetDocument().Revision();const auto placed=window.Session().PlacedPointCount();
    window.RunCommand("grid.edit");spacing->setText("25.4/8");
    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(spacing,&enter);
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(spacing,&escape);
    if(!Explain("real grid Enter/Escape never confirm/cancel drawing",window.Session().PlacedPointCount()==placed
        &&placed==1&&window.Session().GetDocument().Revision()==revision
        &&std::abs(window.Session().Scene().grid.majorSpacingMm-3.175)<1e-12))return false;
    int changes=0;auto connection=window.Session().OnSceneChanged([&]{++changes;});
    auto choice=window.CurrentGridChoice();window.ApplyGridChoice(choice);
    choice.grid.visible=!choice.grid.visible;window.ApplyGridChoice(choice);
    if(!Explain("view-only and identical grid settings do not replace scene",changes==0))return false;
    spacing->setText("25.4/");choice=window.CurrentGridChoice();window.ApplyGridChoice(choice);
    if(!Explain("display sync preserves unfinished expressions",spacing->text()=="25.4/"&&!dock->ApplySpacingExpression(spacing->text())&&changes==0))return false;
    if(!dock->ApplySpacingExpression("2.5"))return false;
    if(!Explain("new spacing replaces scene once and updates snapping grid",changes==1&&window.Session().Scene().grid.majorSpacingMm==2.5))return false;
    details->setChecked(true);if(!Explain("advanced settings expand",!body->isHidden()))return false;
    details->setChecked(false);if(!Explain("common settings remain after collapsing details",body->isHidden()&&!spacing->isHidden()))return false;
    spacing->clearFocus();view.setFocus();window.SelectTool(modeling::DrawingTool::Select);
    choice=window.CurrentGridChoice();choice.grid.originUmm=10.123456;window.ApplyGridChoice(choice);
    const auto front=app::OriginPlaneId(window.Session().GetDocument().Snapshot(),modeling::StandardPlaneKind::ZX);
    if(!front)return false;view.SetSelection(app::SelectionSet{{*front}});window.RunCommand("workplane.set_active");
    const auto origin=window.Session().Scene().grid.origin;
    auto* check=window.findChild<QCheckBox*>("gridVisibility");if(!check)return false;check->click();
    if(!Explain("permanent grid follows plane changes without applying stale origin",window.CurrentGridChoice().grid.originUmm==0
        &&window.Session().Scene().grid.origin==origin&&dock->Choice().grid.originUmm==0))return false;
    Shot(window,"grid-persistent-settings");return PendingActionsFit(window);
}
bool GridProjectionReuse(V2MainWindow& window) {
    window.RunCommand("file.new");auto& view=window.Viewport();
    auto choice=window.CurrentGridChoice();choice.grid.visible=true;choice.showInAllModes=true;
    choice.grid.subdivision=4;window.ApplyGridChoice(choice);
    view.SetViewDirection(ViewDirection::Top);view.SetVisibleWidthMm(100);
    const auto first=Frame(view);const auto builds=view.property("gridProjectionBuilds").toULongLong();
    for(int i=0;i<5;++i)if(Frame(view)!=first)return false;
    if(!Explain("unchanged grid reuses projected strokes",view.property("gridProjectionBuilds").toULongLong()==builds))return false;
    for(int change=0;change<4;++change) {
        if(change==0)view.SetViewCenter({12,8,0});
        if(change==1)view.SetVisibleWidthMm(72);
        if(change==2){choice.grid.originUmm=1.25;window.ApplyGridChoice(choice);}
        if(change==3){choice.grid.majorSpacingMm=4;window.ApplyGridChoice(choice);}
        const auto cached=Frame(view);
        delete view.findChild<QObject*>("gridProjectionCache",Qt::FindDirectChildrenOnly);
        if(!Explain("changed grid/camera matches a fresh projection exactly",cached==Frame(view)))return false;
    }
    return true;
}
bool SelectedWireContrast(V2MainWindow& window) {
    // Keep the crossing clear of navigation controls in the narrow-window fixture.
    window.resize(1400,900);QApplication::processEvents();
    window.RunCommand("file.new");auto& view=window.Viewport();view.SetViewDirection(ViewDirection::Top);view.SetVisibleWidthMm(200);
    view.SetSnapSuppressedByKey(true);window.SelectTool(modeling::DrawingTool::Polyline);
    for(const auto point:std::vector<geometry::Vector3>{{-40,0,0},{40,0,0},{40,20,0}}) {
        const auto at=view.Mapping().Project(point);if(!at)return false;view.ClickAt({at->x,at->y});
    }
    view.FinishTool();window.SelectTool(modeling::DrawingTool::Line);
    for(const auto point:std::vector<geometry::Vector3>{{0,-20,0},{0,20,0}}) {
        const auto at=view.Mapping().Project(point);if(!at)return false;view.ClickAt({at->x,at->y});
    }
    window.SelectTool(modeling::DrawingTool::Select);view.SetSnapSuppressedByKey(false);
    const auto curves=window.Session().Scene().curves;if(curves.size()!=3)return false;
    const auto& curve=curves.front();app::SelectionSet selection;selection.entityIds={curve.entityId};app::SelectionRef ref;
    ref.entityId=curve.entityId;ref.segmentId=curve.segmentId;ref.kind=app::SelectionElementKind::Edge;selection.ordered={ref};view.SetSelection(selection);
    auto display=view.DisplaySettingsNow();display.gridVisible=false;display.workPlaneVisible=false;view.SetDisplaySettings(display);
    for(int i=0;i<3;++i)view.SetAxisVisible(i,false);
    const auto center=view.Mapping().Project({0,0,0});if(!center)return false;const QPointF at(center->x,center->y);
    for(const auto theme:{UiTheme::Windows95,UiTheme::Normal}) {
        window.ApplyTheme(theme);const auto selected=view.SemanticColor(app::SemanticState::Selected);
        view.SetSegmentRoleColors({});
        if(!Explain("selected segment stays visible over a later crossing wire",Near(Frame(view),at,selected,2)))return false;
        for(const QColor role:{QColor(120,0,190),QColor(240,135,20)}) {
        view.SetSegmentRoleColors({{curve.entityId,curve.segmentId,role}});
        const auto image=Frame(view);
        if(!Explain("input role center and separate selection rim are both visible",view.SegmentRoleColorOf(curve.entityId,curve.segmentId)==role
            &&Near(image,at,role,2)&&Near(image,at,selected,5)))return false;
        }
        view.SetDeletionPreview({curve.entityId});
        if(!Explain("deletion red takes precedence over input and selection",view.SegmentRoleColorOf(curve.entityId,curve.segmentId)==QColor(235,65,55)
            &&Near(Frame(view),at,QColor(235,65,55),2)))return false;
        view.SetDeletionPreview({});view.SetSegmentRoleColors({});
        Shot(window,theme==UiTheme::Windows95?"selected-wire-win95":"selected-wire-dark");
        for(bool datum:{false,true}) {
            auto scene=window.Session().Scene();scene.curves.front().datum=datum;scene.curves.front().construction=!datum;
            window.Session().SetScene(std::move(scene));
            if(!Explain("selected dashed and datum strokes remain readable",Near(Frame(view),at,selected,12)))return false;
            view.SetSegmentRoleColors({{curve.entityId,curve.segmentId,QColor(120,0,190)}});
            const auto dashed=Frame(view);
            const auto begin=view.Mapping().Project({-40,0,0});const auto end=view.Mapping().Project({40,0,0});
            if(!begin||!end)return false;
            int gaps=0,ink=0;const int y=int(std::round(at.y()))+3;
            for(int x=int(begin->x)+12;x<int(end->x)-12;++x) {
                if(std::abs(x-at.x())<12)continue; // crossing wire
                if(dashed.pixelColor(x,y)==view.Colors().background)++gaps;else ++ink;
            }
            if(!Explain("role-colored dashed selection retains background gaps and ink",gaps>3&&ink>3))return false;
            view.SetSegmentRoleColors({});
        }
        auto scene=window.Session().Scene();scene.curves.front().datum=false;scene.curves.front().construction=false;
        window.Session().SetScene(std::move(scene));
    }
    return true;
}
bool HiddenOverlayRendering(V2MainWindow& window) {
    auto& view=window.Viewport();base::DeterministicIdGenerator ids(981000);
    const auto shapeId=ids.NextTyped<base::IdKind::Entity>(),imageId=ids.NextTyped<base::IdKind::Entity>();
    modeling::ShapeMesh mesh;modeling::MeshTriangle triangle;triangle.points={geometry::Vector3{-20,-20,0},{20,-20,0},{0,20,0}};
    triangle.normal={0,0,1};mesh.triangles={triangle};modeling::RefreshBounds(mesh);view.SetShapeViews({{shapeId,mesh,true}});
    view.SetViewDirection(ViewDirection::Top);view.SetVisibleWidthMm(100);view.SetSelection(app::SelectionSet{{shapeId}});
    auto display=view.DisplaySettingsNow();display.selectionOnly=true;view.SetDisplaySettings(display);
    const auto before=Frame(view);const auto counts=view.InteractionCounts();
    V2ImageView image;image.entityId=imageId;image.image={1,1,{0xff00ff00u},1};
    modeling::ImageTriangle item;item.mesh=triangle;for(auto& p:item.mesh.points)p.z=1;image.triangles={item};
    view.SetImageViews({image});view.SetAnalysisViews({V2Viewport::AnalysisView{}});const auto after=Frame(view);
    if(!Explain("hidden image and empty analysis preserve exact picture",before==after))return false;
    std::cout<<"hidden overlays backend="<<view.RenderBackend()<<", software rasterizations="
        <<view.InteractionCounts().rasterizations-counts.rasterizations<<std::endl;
    if(QString::fromUtf8(view.RenderBackend())!="CPU"&&!Explain("hidden overlays do not force CPU rasterization",counts.rasterizations==view.InteractionCounts().rasterizations))return false;
    auto analysis=V2Viewport::AnalysisView{};analysis.entityId=shapeId;
    analysis.data=std::make_shared<const modeling::SurfaceAnalysisData>();
    analysis.triangles=std::make_shared<const std::vector<modeling::MeshTriangle>>(std::vector{triangle});
    view.SetAnalysisViews({analysis});display.shapesVisible=false;view.SetDisplaySettings(display);
    const auto hiddenCounts=view.InteractionCounts();Frame(view);
    if(!Explain("hidden shapes and analysis do not rasterize",hiddenCounts.rasterizations==view.InteractionCounts().rasterizations))return false;
    analysis.mode=app::SurfaceAnalysisMode::IsoCurves;view.SetAnalysisViews({analysis});
    display.shapesVisible=true;view.SetDisplaySettings(display);
    const auto lineCounts=view.InteractionCounts();
    if(!Explain("showing shapes again restores shading",Frame(view)==before))return false;
    if(QString::fromUtf8(view.RenderBackend())!="CPU"&&!Explain("line-only analysis retains GPU surface shading",lineCounts.rasterizations==view.InteractionCounts().rasterizations))return false;
    analysis.lines={{{{-20,-20,1},{20,-20,1}},app::Rgb{255,0,0},2.}};
    view.SetAnalysisViews({analysis});
    if(!Explain("analysis lines still draw over GPU shading",Frame(view)!=before))return false;
    view.SetAnalysisViews({});
    display.selectionOnly=false;view.SetDisplaySettings(display);const auto visible=Frame(view);
    image.image.pixels[0]=0xffff0000u;view.SetImageViews({image});
    return Explain("visible image keeps its depth/compositing and replacement pixels",visible!=Frame(view));
}
bool PresentationUpdates(V2MainWindow& window){
    window.RunCommand("file.new");auto& view=window.Viewport();view.SetViewDirection(ViewDirection::Top);
    if(!DrawRectangleByHand(window))return false;window.RunCommand("selection.activate");
    QApplication::processEvents();const auto first=Frame(view);const auto paints=view.property("nativeViewportFrames").toULongLong();
    view.PanByPixels(25,15);view.repaint();view.CompletePresentationForTest();
    const auto moved=Frame(view);if(!Explain("presentation capture follows camera changes",first!=moved))return false;
    auto* canvas=view.findChild<QWidget*>("nativeViewportCanvas");const bool native=canvas&&canvas->isVisible()&&canvas->property("nativeReady").toBool();
    if(native&&!Explain("native repaint paints a fresh frame",view.property("nativeViewportFrames").toULongLong()>paints))return false;
    app::SelectionSet selected;for(const auto& curve:window.Session().Scene().curves)selected.entityIds.push_back(curve.entityId);
    view.SetSelection(selected);view.repaint();const auto marked=Frame(view);
    if(!Explain("native and fallback captures include selection",marked!=moved&&!window.grab().isNull()&&!view.grab().isNull()))return false;
    window.resize(window.width()+80,window.height()+60);QApplication::processEvents();
    const auto resized=Frame(view);if(!Explain("resize preserves complete viewport capture",resized.size()==view.size()&&resized!=marked))return false;
    QApplication::processEvents();const auto idle=view.property("nativeViewportFrames").toULongLong();
    QEventLoop settle;QTimer::singleShot(150,&settle,&QEventLoop::quit);settle.exec();
    return Explain("presentation does not schedule an idle repaint loop",!native||view.property("nativeViewportFrames").toULongLong()<=idle+2);
}
// Explicit benchmark only: fixed-size interactive repaint with GPU completion.
bool ViewportFrameBenchmark(V2MainWindow& window)
{
    const auto model=qEnvironmentVariable("KACHACAD_VIEWPORT_PERF_MODEL");
    if(model!="empty"&&!window.OpenDocumentFile(model))return false;
    window.resize(2560,1440);window.show();QApplication::processEvents();
    auto& view=window.Viewport();view.SetViewDirection(ViewDirection::Isometric);
    view.FitToDocument();view.SetVisibleWidthMm(200);view.SetGridSuppressedByMode(false);
    auto grid=window.CurrentGridChoice();grid.grid.visible=true;grid.showInAllModes=true;
    const auto revision=window.Session().GetDocument().Revision();
    QJsonArray results;
    const bool events=qEnvironmentVariableIsSet("KACHACAD_VIEWPORT_PERF_EVENTS");
    const int frames=std::clamp(qEnvironmentVariableIntValue("KACHACAD_VIEWPORT_PERF_FRAMES"),16,240);
    for(const auto& configuration:std::vector<std::pair<QString,int>>{{"off",0},{"major",1},{"subdivision",4},{"near-edge",4}}) {
        // Keep the subdivision actually visible (>=6px), regardless of dock widths.
        grid.grid.majorSpacingMm=configuration.second==1?10:200.*40/std::max(1,view.width());
        grid.grid.subdivision=configuration.second==4?4:0;window.ApplyGridChoice(grid);
        auto display=view.DisplaySettingsNow();display.gridVisible=configuration.second!=0;
        view.SetDisplaySettings(display);view.SetViewDirection(ViewDirection::Isometric);
        if(configuration.first=="near-edge"){view.SetViewDirection(ViewDirection::Front);view.OrbitByPixels(0,2);}
        const auto orientation=view.Orientation();
        for(const auto* operation:{"orbit","pan"}) {
            view.SetOrientation(orientation);QApplication::processEvents();
            QImage image(view.size(),QImage::Format_ARGB32);view.render(&image);
            const QPointF start(view.width()*.4,view.height()*.6);
            const auto modifiers=QString::fromLatin1(operation)=="orbit"?Qt::ShiftModifier:Qt::NoModifier;
            if(events){QMouseEvent press(QEvent::MouseButtonPress,start,view.mapToGlobal(start.toPoint()),Qt::MiddleButton,Qt::MiddleButton,modifiers);QApplication::sendEvent(&view,&press);}
            const auto counts=view.InteractionCounts();std::vector<double> samples,workSamples;
            const auto nativeFrames=view.property("nativeViewportFrames").toULongLong(),readbacks=view.PresentationReadbacks();
            for(int i=0;i<frames+4;++i) {
                QElapsedTimer timer;timer.start();
                const double sign=i%2?1:-1;
                if(events){const auto at=start+QPointF(i%2?3:0,i%2?1:0);
                    QMouseEvent move(QEvent::MouseMove,at,view.mapToGlobal(at.toPoint()),Qt::NoButton,Qt::MiddleButton,modifiers);QApplication::sendEvent(&view,&move);}
                else if(QString::fromLatin1(operation)=="orbit")view.OrbitByPixels(sign*3,sign);
                else view.PanByPixels(sign*3,sign);
                view.repaint();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                view.CompletePresentationForTest(); // include Qt composition and completed GPU work
                if(i>=4){samples.push_back(double(timer.nsecsElapsed())/1e6);workSamples.push_back(view.property("presentationWorkMs").toDouble());}
            }
            std::sort(samples.begin(),samples.end());
            std::sort(workSamples.begin(),workSamples.end());
            const auto paintedFrames=view.property("nativeViewportFrames").toULongLong()-nativeFrames;
            if(nativeFrames>0)std::cout<<"native frames="<<paintedFrames<<" requested="<<frames+4<<" readbacks="<<view.PresentationReadbacks()-readbacks<<std::endl;
            if(nativeFrames>0&&!Explain("every requested native frame is painted without application readback",paintedFrames>=std::uint64_t(frames+4)&&view.PresentationReadbacks()==readbacks))return false;
            if(events){QMouseEvent release(QEvent::MouseButtonRelease,start,view.mapToGlobal(start.toPoint()),Qt::MiddleButton,Qt::NoButton,modifiers);QApplication::sendEvent(&view,&release);}
            const auto percentile=[&](double p){return samples[std::min(samples.size()-1,std::size_t(p*samples.size()))];};
            const auto over=std::count_if(samples.begin(),samples.end(),[](double ms){return ms>1000./60;});
            QJsonObject row{{"grid",configuration.first},{"operation",operation},{"frames",frames},
                {"median_ms",percentile(.5)},{"p95_ms",percentile(.95)},{"p99_ms",percentile(.99)},
                {"max_ms",samples.back()},{"over_16_67_ms",int(over)},
                {"backend",QString::fromUtf8(view.RenderBackend())},
                {"native_frames",double(paintedFrames)},{"application_readbacks",double(view.PresentationReadbacks()-readbacks)},
                {"paint_gpu_complete_median_ms",workSamples[workSamples.size()/2]},
                {"paint_gpu_complete_p95_ms",workSamples[std::size_t(.95*workSamples.size())]},
                {"software_rasterizations",double(view.InteractionCounts().rasterizations-counts.rasterizations)},
                {"shapes",view.ShapeViewCount()},{"triangles",view.ShapeTriangleCount()}};
            const auto& g=window.Session().Scene().grid;const double spacing=g.majorSpacingMm/(g.subdivision>1?g.subdivision:1);
            row.insert("minor_u_spacing_px",modeling::GridLineSpacingPx(view.Mapping(),g.origin,g.vDirection,g.uDirection*spacing));
            row.insert("minor_v_spacing_px",modeling::GridLineSpacingPx(view.Mapping(),g.origin,g.uDirection,g.vDirection*spacing));
            results.push_back(row);std::cout<<QJsonDocument(row).toJson(QJsonDocument::Compact).constData()<<std::endl;
        }
    }
    const auto output=qEnvironmentVariable("KACHACAD_VIEWPORT_PERF_OUTPUT");
    if(!output.isEmpty()) {
        QFile file(output);if(!file.open(QIODevice::WriteOnly))return false;
        QJsonObject report{{"model",model},{"viewport_width",view.width()},{"viewport_height",view.height()},
            {"device_pixel_ratio",view.devicePixelRatioF()},{"measure","camera input + Qt repaint/composition events + GPU completion"},
                {"qt_mouse_events",events},{"results",results}};file.write(QJsonDocument(report).toJson());
    }
    return Explain("benchmark keeps document and rebuilt geometry unchanged",
        revision==window.Session().GetDocument().Revision()&&window.RebuildProblems().isEmpty());
}
}
