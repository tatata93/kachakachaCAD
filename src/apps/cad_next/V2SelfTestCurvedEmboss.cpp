#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2CurvedEmbossTool.h"
#include "kachakacha/app/Selection.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QEvent>
#include <QPointF>
#include <QWidget>
#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>
namespace kachakacha::v2::selftest {
namespace {
bool CaseCurvedEmboss(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const auto outer=DrawRectangleAtByHand(window,0.3,0.3,0.7,0.7);
    if(!Explain("outer rectangle",!outer.IsNil()&&ClickOnCurveOf(window,outer)))return false;
    window.RunCommand("surface.create");if(!Explain("create support",window.HandleToolKey(Qt::Key_Return,nullptr)))return false;
    const auto surfaces=app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),domain::EntityKind::GuideSurface);
    if(!Explain("one surface",surfaces.entityIds.size()==1))return false;
    const auto inner=DrawRectangleAtByHand(window,0.4,0.4,0.6,0.6);if(!Explain("inner rectangle",!inner.IsNil()))return false;
    window.Viewport().SetSelection({});window.ActivateCommand("part.curved_emboss");
    auto* tool=dynamic_cast<V2CurvedEmbossTool*>(window.findChild<QWidget*>(QStringLiteral("curvedEmbossPanel")));
    if(!Explain("opened tool",tool!=nullptr))return false;
    app::SelectionRef ref;ref.entityId=surfaces.entityIds.front();ref.kind=app::SelectionElementKind::Face;
    tool->findChild<QDoubleSpinBox*>(QStringLiteral("embossHeight"))->setValue(2);
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&window.Viewport(),&escape);
    if(!Explain("escape preserves tool and height",tool->isVisible()&&tool->findChild<QDoubleSpinBox*>(QStringLiteral("embossHeight"))->value()==2))return false;
    bool supportClicked=false;
    for(const auto& shape:window.Viewport().ShapeViews())if(shape.entityId==ref.entityId&&!shape.mesh.triangles.empty()){
        const auto& t=shape.mesh.triangles.front();const auto screen=window.Viewport().Mapping().Project((t.points[0]+t.points[1]+t.points[2])*(1.0/3.0));
        if(!screen)return false;const QPointF p(screen->x,screen->y);
        QMouseEvent click(QEvent::MouseButtonPress,p,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&window.Viewport(),&click);supportClicked=true;break;
    }
    if(!Explain("support selected in viewport",supportClicked))return false;
    bool clicked=false;
    for(const auto& curve:window.Session().Scene().curves)if(curve.entityId==inner){
        const auto screen=window.Viewport().Mapping().Project(curve.segment.Evaluate(0.5));if(!screen)return false;
        const QPointF p(screen->x,screen->y);QMouseEvent click(QEvent::MouseButtonPress,p,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&window.Viewport(),&click);clicked=true;break;
    }
    if(!Explain("viewport input keeps tool",clicked&&tool->isVisible()))return false;
    const auto revision=window.Session().GetDocument().Revision();
    if(!Explain("preview without document changes",tool->Preview()&&window.Viewport().ToolPreviewFaceCount()>0
        &&window.Session().GetDocument().Revision()==revision))return false;
    auto* output=tool->findChild<QComboBox*>(QStringLiteral("embossOutput"));output->setCurrentIndex(1);
    if(!tool->Preview()||window.Viewport().ToolPreviewFaceCount()!=0)return false;
    const auto before=CountOfKind(window,domain::EntityKind::Wire);
    if(!tool->Commit()||CountOfKind(window,domain::EntityKind::Part)!=0
        ||CountOfKind(window,domain::EntityKind::Wire)!=before+1)return false;
    window.RunCommand("edit.undo");if(CountOfKind(window,domain::EntityKind::Wire)!=before)return false;
    tool=V2CurvedEmbossTool::Open(window);
    if(!tool->SetSupport(ref)||!tool->AddWire(inner))return false;
    tool->findChild<QComboBox*>(QStringLiteral("embossOutput"))->setCurrentIndex(0);
    if(!tool->Commit()||CountOfKind(window,domain::EntityKind::Part)!=1)return false;
    window.RunCommand("edit.undo");
    if(!Explain("solid and boundary undo atomically",CountOfKind(window,domain::EntityKind::Part)==0
        &&CountOfKind(window,domain::EntityKind::Wire)==before))return false;
    window.RunCommand("edit.redo");
    return Explain("frozen curved solid survives reopen",window.SaveAndReopen(QStringLiteral("kacha_selftest_emboss.kcd2"))
        &&window.RebuildProblems().isEmpty()&&CountOfKind(window,domain::EntityKind::Part)==1);
}
}
std::vector<SelfTestCase> CurvedEmbossCases(){return {{"HP-EMB-01 curved emboss selection preview outputs undo",CaseCurvedEmboss}};}
}
