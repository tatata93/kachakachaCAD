#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2ImageTool.h"
#include "kachakacha/kernel/OcctImage.h"
#include <QApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QString>
#include <QFile>
#include <QPointF>
#include <QMouseEvent>
#include <QEvent>
#include <QPixmap>
#include <QDir>
#include <QWidget>
#include <QSize>
#include <cmath>
namespace kachakacha::v2::selftest {
bool MakeCurvedGuideSurface(V2MainWindow&);
namespace {
bool ImageClick(V2MainWindow& window,const geometry::Vector3& point) {
    const auto p=window.Viewport().Mapping().Project(point);if(!p)return false;
    const QPointF local(p->x,p->y);QMouseEvent event(QEvent::MouseButtonPress,local,local,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&window.Viewport(),&event);return true;
}
bool CaseImagePersistence(V2MainWindow& window) {
    struct RestoreSize { V2MainWindow& window; QSize size; ~RestoreSize(){window.resize(size);} } restore{window,window.size()};
    window.resize(1600,1000);QApplication::processEvents();
    QTemporaryDir folder;if(!folder.isValid())return false;
    const auto path=folder.filePath(QStringLiteral("reference.png"));QImage bitmap(80,40,QImage::Format_ARGB32);bitmap.fill(0xffff4020);
    if(!bitmap.save(path))return false;
    window.RunCommand("image.place");auto* panel=window.findChild<QWidget*>(QStringLiteral("imagePlacementPanel"));
    auto* tool=dynamic_cast<V2ImageTool*>(panel);if(!Explain("tool-first image pane",tool!=nullptr))return false;
    const auto before=window.Session().GetDocument().Revision();
    if(!Explain("load image and preview",tool->LoadImage(path)&&window.Viewport().ImageViews().size()==1))return false;
    if(!Explain("preview does not mutate document",window.Session().GetDocument().Revision()==before))return false;
    panel->findChild<QPushButton*>(QStringLiteral("imagePixelLength"))->click();
    window.Viewport().SetViewDirection(ViewDirection::Top);window.Viewport().SetVisibleWidthMm(200);
    for(const auto point:{geometry::Vector3{12.5,37.5,0},geometry::Vector3{37.5,37.5,0},geometry::Vector3{0,0,0},geometry::Vector3{40,0,0}}){
        if(!ImageClick(window,point))return false;
    }
    if(!Explain("two image points match CAD distance",std::abs(panel->findChild<QDoubleSpinBox*>(QStringLiteral("imageWidth"))->value()-160)<1e-5))return false;
    const auto corner=window.Viewport().ImageViews().back().triangles.front().mesh.points[0];
    panel->findChild<QPushButton*>(QStringLiteral("imageAnchor"))->click();
    if(!ImageClick(window,{40,20,0}))return false;
    if(!Explain("choosing anchor keeps image still",geometry::Distance(corner,window.Viewport().ImageViews().back().triangles.front().mesh.points[0])<1e-8))return false;
    if(!ImageClick(window,{3,4,0}))return false;
    panel->findChild<QDoubleSpinBox*>(QStringLiteral("imageWidth"))->setValue(20);
    QApplication::processEvents();window.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-image-panel.png"));
    if(!Explain("commit image",tool->Commit()))return false;
    QApplication::processEvents();QFile::remove(path);
    if(!Explain("image-only document enables fit",window.CommandEnabled("view.fit_all",nullptr)))return false;
    window.RunCommand("view.fit_all");
    if(!Explain("fit includes image bounds",window.Viewport().VisibleWidthMm()<100))return false;
    if(!Explain("image persisted after original deleted",window.SaveAndReopen(QStringLiteral("image-selftest.kcd2"))&&window.Viewport().ImageViews().size()==1))return false;
    const auto& view=window.Viewport().ImageViews().front();const auto id=view.entityId;
    const auto screen=window.Viewport().Mapping().Project({3,4,0});if(!screen)return false;
    window.Viewport().SelectAt(QPointF(screen->x,screen->y),Qt::NoModifier);
    if(!Explain("image is selectable in 3D",app::IsSelected(window.Viewport().Selection(),id)))return false;
    auto* edit=V2ImageTool::Open(window);auto* width=edit->findChild<QDoubleSpinBox*>(QStringLiteral("imageWidth"));
    if(!Explain("reopen same width",std::abs(width->value()-20)<1e-6))return false;
    width->setValue(37);edit->findChild<QPushButton*>(QStringLiteral("imageCancel"))->click();QApplication::processEvents();
    auto* again=V2ImageTool::Open(window);if(!Explain("cancel kept old placement",std::abs(again->findChild<QDoubleSpinBox*>(QStringLiteral("imageWidth"))->value()-20)<1e-6))return false;
    again->findChild<QDoubleSpinBox*>(QStringLiteral("imageWidth"))->setValue(25);
    again->findChild<QCheckBox*>(QStringLiteral("imageMirror"))->setChecked(true);
    again->findChild<QDoubleSpinBox*>(QStringLiteral("imageOpacity"))->setValue(65);
    again->findChild<QPushButton*>(QStringLiteral("imageAnchor"))->click();
    if(!ImageClick(window,{3,4,0})||!ImageClick(window,{5,10,0})||!again->Commit())return false;
    const auto& doc=window.Session().GetDocument();const auto* entity=doc.FindEntity(id);
    const auto* saved=entity?doc.FindFeature(entity->createdBy):nullptr;if(!saved)return false;
    const auto* edited=std::get_if<domain::CreateImageDefinition>(&saved->definition);
    if(edited)Note(("edited origin="+std::to_string(edited->origin.x)+","+std::to_string(edited->origin.y)+" opacity="+std::to_string(edited->opacity)+" mirror="+std::to_string(edited->mirrorHorizontal)).c_str());
    if(!Explain("post-placement mirror position transparency",edited&&edited->mirrorHorizontal&&std::abs(edited->opacity-.35)<1e-9&&geometry::Distance(edited->origin,{5,10,0})<1e-9))return false;
    window.RunCommand("edit.undo");window.RunCommand("edit.redo");
    window.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-image-placement.png"));
    return Explain("undo redo retains image",window.Viewport().ImageViews().size()==1);
}
bool CaseImageSurface(V2MainWindow& window) {
    if(!MakeCurvedGuideSurface(window))return false;
    const auto shapes=window.Viewport().ShapeViews();if(shapes.empty()||shapes.front().mesh.triangles.empty())return false;
    const auto& triangle=shapes.front().mesh.triangles.front();app::SelectionRef ref;ref.entityId=shapes.front().entityId;
    ref.kind=app::SelectionElementKind::Face;ref.pickedFaceIndex=triangle.faceIndex;ref.hitPoint=(triangle.points[0]+triangle.points[1]+triangle.points[2])*(1.0/3);
    QTemporaryDir folder;const auto path=folder.filePath(QStringLiteral("surface.png"));QImage bitmap(32,32,QImage::Format_ARGB32);bitmap.fill(0xff00bbee);bitmap.save(path);
    auto* tool=V2ImageTool::Open(window);if(!tool->LoadImage(path)||!tool->SetTarget(ref))return false;
    auto* mode=tool->findChild<QComboBox*>(QStringLiteral("imageMapping"));
    for(int i:{0,1}){mode->setCurrentIndex(i);tool->Preview();if(!Explain("both curved mapping previews",!window.Viewport().ImageViews().empty()))return false;}
    const auto& imageMesh=window.Viewport().ImageViews().back().triangles;
    double mismatch=0;
    if(imageMesh.size()==shapes.front().mesh.triangles.size())for(std::size_t i=0;i<imageMesh.size();++i)
        for(const auto& p:imageMesh[i].mesh.points){double nearest=1e99;for(const auto& q:shapes.front().mesh.triangles[i].points)nearest=std::min(nearest,geometry::Distance(p,q));mismatch=std::max(mismatch,nearest);}
    Note(("image triangles="+std::to_string(imageMesh.size())+" source="+std::to_string(shapes.front().mesh.triangles.size())+" mismatch="+std::to_string(mismatch)).c_str());
    if(!Explain("captured face tessellation retained without decal flicker",imageMesh.size()==shapes.front().mesh.triangles.size()&&mismatch<1e-8))return false;
    window.Viewport().SetViewDirection(ViewDirection::Isometric);window.RunCommand("view.fit_all");
    QApplication::processEvents();window.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-image-curved.png"));
    if(!Explain("commit wrapped image",tool->Commit()))return false;
    if(!window.SaveAndReopen(QStringLiteral("surface-image-selftest.kcd2")))return false;
    const auto& doc=window.Session().GetDocument();
    for(const auto& f:doc.Snapshot().features)if(const auto* d=std::get_if<domain::CreateImageDefinition>(&f.definition))
        return Explain("curved face stored independently",d->followSurface&&!d->faceBrep.empty()&&!window.Viewport().ImageViews().empty());
    return false;
}
bool CaseImageDrag(V2MainWindow& window) {
    struct RestoreSize { V2MainWindow& window; QSize size; ~RestoreSize(){window.resize(size);} } restore{window,window.size()};
    window.resize(1600,1000);QApplication::processEvents();
    QTemporaryDir folder;const auto path=folder.filePath(QStringLiteral("drag.png"));
    QImage bitmap(80,40,QImage::Format_ARGB32);bitmap.fill(0xffee6644);if(!bitmap.save(path))return false;
    auto* tool=V2ImageTool::Open(window);if(!tool->LoadImage(path)||!tool->Commit())return false;
    QApplication::processEvents();
    const auto id=window.Viewport().ImageViews().front().entityId;
    window.Viewport().SetViewDirection(ViewDirection::Top);window.Viewport().SetVisibleWidthMm(300);
    const auto center=window.Viewport().Mapping().Project({40,20,0});if(!center)return false;
    window.Viewport().SelectAt(QPointF(center->x,center->y),Qt::NoModifier);
    tool=V2ImageTool::Open(window);QApplication::processEvents();
    auto send=[&](QEvent::Type type,const geometry::Vector3& point){
        const auto p=window.Viewport().Mapping().Project(point);if(!p)return false;
        const QPointF local(p->x,p->y);
        QMouseEvent event(type,local,local,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
            type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&window.Viewport(),&event);return true;
    };
    auto corner=[&]{return window.Viewport().ImageViews().back().triangles.front().mesh.points[0];};
    const auto original=corner();const auto revision=window.Session().GetDocument().Revision();
    if(!send(QEvent::MouseButtonPress,{40,20,0})||!send(QEvent::MouseMove,{53,27,0}))return false;
    Note(("drag displacement="+std::to_string(corner().x-original.x)+","+std::to_string(corner().y-original.y)+
        " viewport width="+std::to_string(window.Viewport().width())).c_str());
    if(!Explain("image follows before mouse release",geometry::Distance(corner(),original+geometry::Vector3{13,7,0})<1e-6))return false;
    if(!send(QEvent::MouseMove,{62,31,0})||!send(QEvent::MouseButtonRelease,{62,31,0}))return false;
    if(!Explain("drag is preview only",window.Session().GetDocument().Revision()==revision&&
        geometry::Distance(corner(),original+geometry::Vector3{22,11,0})<1e-6))return false;
    tool->findChild<QPushButton*>(QStringLiteral("imageCancel"))->click();QApplication::processEvents();
    if(!Explain("cancel restores image placement",geometry::Distance(corner(),original)<1e-6))return false;
    tool=V2ImageTool::Open(window);QApplication::processEvents();
    if(!send(QEvent::MouseButtonPress,{40,20,0})||!send(QEvent::MouseMove,{53,27,0})||
        !send(QEvent::MouseButtonRelease,{53,27,0})||!tool->Commit())return false;
    QApplication::processEvents();
    if(!Explain("drag updates same image",window.Viewport().ImageViews().size()==1&&
        window.Viewport().ImageViews().front().entityId==id&&geometry::Distance(corner(),original+geometry::Vector3{13,7,0})<1e-6))return false;
    window.RunCommand("edit.undo");
    if(!Explain("undo entire drag",geometry::Distance(corner(),original)<1e-6))return false;
    window.RunCommand("edit.redo");
    return Explain("redo entire drag",geometry::Distance(corner(),original+geometry::Vector3{13,7,0})<1e-6);
}
}
std::vector<SelfTestCase> ImageCases(){return {{"HP-IMG-01 image placement persistence cancel and edit",&CaseImagePersistence},{"HP-IMG-02 curved projection and wrapping",&CaseImageSurface},{"HP-IMG-03 image drag preview cancel commit undo",&CaseImageDrag}};}
}
