#include "V2SelfTest.h"
#include "V2InstructionScene.h"
#include <QString>
#include <QUuid>
#include <QApplication>
#include <QMouseEvent>
#include <QEvent>
#include <QImage>
#include <QFont>
#include <QFontMetricsF>
#include <QLineF>
#include <QPointF>
#include <cmath>
#include <algorithm>
namespace kachakacha::v2::selftest {
namespace {
void SceneMouse(V2InstructionScene& view,QEvent::Type type,QPointF at,
                Qt::MouseButton button,Qt::MouseButtons held){
    QMouseEvent event(type,at,at,button,held,Qt::NoModifier);
    QApplication::sendEvent(&view,&event);QApplication::processEvents();
}
void SceneClick(V2InstructionScene& view,QPointF at){
    SceneMouse(view,QEvent::MouseButtonPress,at,Qt::LeftButton,Qt::LeftButton);
    SceneMouse(view,QEvent::MouseButtonRelease,at,Qt::LeftButton,Qt::NoButton);
}
void SceneDrag(V2InstructionScene& view,QPointF from,QPointF to){
    SceneMouse(view,QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
    SceneMouse(view,QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);
    SceneMouse(view,QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);
}
int Ink(const QImage& image){
    int count=0;
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
        if(qGray(image.pixel(x,y))<200)++count;
    return count;
}
bool CancelledMarkDrag(V2InstructionScene& view,InstructionPage& page,int index,QPointF from){
    const auto before=page.marks[index];
    SceneMouse(view,QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
    SceneMouse(view,QEvent::MouseMove,from+QPointF(42,16),Qt::NoButton,Qt::LeftButton);
    if((page.marks[index].start-before.start).Length()+(page.marks[index].end-before.end).Length()<1e-5)return false;
    view.CancelInput();
    SceneMouse(view,QEvent::MouseButtonRelease,from+QPointF(42,16),Qt::LeftButton,Qt::NoButton);
    return (page.marks[index].start-before.start).Length()<1e-8&&
        (page.marks[index].end-before.end).Length()<1e-8&&page.marks[index].font==before.font;
}
bool ArrowInteractions(V2MainWindow&){
    InstructionPage page;V2InstructionScene view(nullptr);view.resize(640,480);
    view.SetPage(&page);view.show();QApplication::processEvents();view.SetTool("arrow");
    SceneMouse(view,QEvent::MouseButtonPress,{100,100},Qt::LeftButton,Qt::LeftButton);
    SceneMouse(view,QEvent::MouseMove,{260,190},Qt::NoButton,Qt::LeftButton);
    if(!Explain("arrow draft does not add a mark",page.marks.empty()))return false;
    if(!Explain("arrow draft is visible only in editor",Ink(view.Render(view.size(),true))>20&&Ink(view.Render(view.size(),false))==0))return false;
    SceneMouse(view,QEvent::MouseButtonRelease,{260,190},Qt::LeftButton,Qt::NoButton);
    if(!Explain("drag release creates one arrow",page.marks.size()==1&&page.marks.front().arrow))return false;
    view.SetTool("arrow");SceneClick(view,{400,80});
    if(!Explain("first short click keeps arrow pending",page.marks.size()==1))return false;
    SceneClick(view,{500,170});
    if(!Explain("two clicks create one more arrow",page.marks.size()==2&&view.SelectedMark()==1))return false;
    view.SetTool("move");
    SceneClick(view,(view.Project(page.marks[0].start)+view.Project(page.marks[0].end))*.5);
    if(!Explain("arrow body can be selected",view.SelectedMark()==0))return false;
    const auto beforeHandle=page.marks[0];const auto handle=view.Project(beforeHandle.start);
    SceneDrag(view,handle,handle+QPointF(30,15));
    if(!Explain("start handle moves only start",(page.marks[0].start-beforeHandle.start).Length()>1e-5&&
                (page.marks[0].end-beforeHandle.end).Length()<1e-8))return false;
    const auto beforeTip=page.marks[0];const auto tip=view.Project(beforeTip.end);
    SceneDrag(view,tip,tip+QPointF(-10,25));
    if(!Explain("tip handle moves only tip",(page.marks[0].end-beforeTip.end).Length()>1e-5&&
                (page.marks[0].start-beforeTip.start).Length()<1e-8))return false;
    const auto beforeBody=page.marks[0];
    const auto body=(view.Project(beforeBody.start)+view.Project(beforeBody.end))*.5;
    SceneDrag(view,body,body+QPointF(25,-20));
    const auto movedStart=page.marks[0].start-beforeBody.start,movedEnd=page.marks[0].end-beforeBody.end;
    if(!Explain("body drag preserves arrow direction and length",movedStart.Length()>1e-5&&(movedStart-movedEnd).Length()<1e-8))return false;
    if(!Explain("Escape restores arrow endpoint during drag",CancelledMarkDrag(view,page,0,view.Project(page.marks[0].start))))return false;
    const auto cancelBody=(view.Project(page.marks[0].start)+view.Project(page.marks[0].end))*.5;
    if(!Explain("Escape restores entire arrow during body drag",CancelledMarkDrag(view,page,0,cancelBody)))return false;
    view.SetTool("arrow");
    SceneMouse(view,QEvent::MouseButtonPress,{320,330},Qt::LeftButton,Qt::LeftButton);
    SceneMouse(view,QEvent::MouseMove,{380,360},Qt::NoButton,Qt::LeftButton);
    view.CancelInput();
    SceneMouse(view,QEvent::MouseButtonRelease,{380,360},Qt::LeftButton,Qt::NoButton);
    if(!Explain("Escape cancel path prevents release from committing draft",page.marks.size()==2))return false;
    view.SetTool("arrow");SceneDrag(view,{100,300},{230,380});
    if(!Explain("arrow tool works again after cancellation",page.marks.size()==3))return false;
    const auto beforeNew=page.marks.back();const auto existingTip=view.Project(beforeNew.end);
    SceneDrag(view,existingTip,existingTip+QPointF(40,-20));
    if(!Explain("arrow tool creates at an existing endpoint instead of editing it",page.marks.size()==4&&
                (page.marks[2].end-beforeNew.end).Length()<1e-8))return false;
    const auto beforeText=page.marks.back();view.SetTool("text",QStringLiteral("4"));
    SceneClick(view,view.Project(beforeText.start));
    if(!Explain("text tool places at an arrow endpoint instead of editing it",page.marks.size()==5&&
                !page.marks.back().arrow&&(page.marks[3].start-beforeText.start).Length()<1e-8))return false;
    page.marks.resize(1000);view.Refresh();int refusals=0;
    view.refused=[&refusals](const QString& text){if(!text.isEmpty())++refusals;};
    view.SetTool("arrow");SceneDrag(view,{50,400},{150,450});
    view.SetTool("text",QStringLiteral("overflow"));SceneClick(view,{100,250});
    return Explain("scene mark limit refuses arrow and text with a reason",page.marks.size()==1000&&refusals==2);
}
bool TextFormatting(V2MainWindow&){
    InstructionPage page;V2InstructionScene view(nullptr);view.resize(640,480);
    view.SetPage(&page);view.show();QApplication::processEvents();
    QFont small(QStringLiteral("Arial"),12);view.SetTextFont(small);view.SetTool("text",QStringLiteral("Assembly"));
    SceneClick(view,{180,230});
    if(!Explain("text placement keeps selected font",page.marks.size()==1&&!page.marks[0].arrow&&page.marks[0].font==small))return false;
    const int smallInk=Ink(view.Render(view.size(),false));page.marks.clear();view.SetPage(&page);
    QFont large(QStringLiteral("Arial"),38);large.setBold(true);view.SetTextFont(large);view.SetTool("text",QStringLiteral("Assembly"));
    view.CancelInput();SceneClick(view,{180,230});
    if(!Explain("text keeps font family size and weight after cancellation",page.marks.size()==1&&
                page.marks[0].font.family()==large.family()&&page.marks[0].font.pointSize()==38&&page.marks[0].font.bold()))return false;
    const int largeInk=Ink(view.Render(view.size(),false));
    view.Refresh();QApplication::processEvents();
    if(!Explain("larger bold text is rendered with more visible ink",smallInk>0&&largeInk>smallInk*2&&page.marks[0].font==large))return false;
    view.SetTool("move");auto drawnFont=large;
    drawnFont.setPixelSize(std::max(1,int(std::round(large.pointSizeF()*4/3*view.width()/1400))));
    const auto bounds=QFontMetricsF(drawnFont).boundingRect(page.marks[0].text).translated(view.Project(page.marks[0].start));
    const QPointF farText(bounds.right()-3,bounds.center().y());SceneClick(view,farText);
    if(!Explain("text can be selected far from its anchor",QLineF(farText,view.Project(page.marks[0].start)).length()>12&&view.SelectedMark()==0))return false;
    return Explain("Escape restores text placement during drag",CancelledMarkDrag(view,page,0,farText));
}
bool CancelPartDrag(V2MainWindow&){
    auto asset=std::make_shared<InstructionAsset>();modeling::MeshTriangle a,b;
    a.points={geometry::Vector3{-1,-1,0},geometry::Vector3{1,-1,0},geometry::Vector3{1,1,0}};
    b.points={geometry::Vector3{-1,-1,0},geometry::Vector3{1,1,0},geometry::Vector3{-1,1,0}};
    asset->mesh.triangles={a,b};asset->mesh.edges={{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0},{-1,-1,0}}};
    InstructionPage page;page.pitch=1.5707963267948966;page.span=4;page.parts.push_back({QUuid::createUuid(),asset});
    V2InstructionScene view(nullptr);view.resize(640,480);view.SetPage(&page);view.SetTool("move");view.show();QApplication::processEvents();
    const QPointF from(view.width()*.5,view.height()*.5);const auto before=page.parts[0].offset;
    SceneMouse(view,QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
    SceneMouse(view,QEvent::MouseMove,from+QPointF(32,15),Qt::NoButton,Qt::LeftButton);
    if(!Explain("part drag previews changed placement",view.Selected()==0&&(page.parts[0].offset-before).Length()>1e-5))return false;
    view.CancelInput();SceneMouse(view,QEvent::MouseButtonRelease,from+QPointF(32,15),Qt::LeftButton,Qt::NoButton);
    return Explain("Escape restores part before release",(page.parts[0].offset-before).Length()<1e-8);
}

}
std::vector<SelfTestCase> InstructionArrowCases(){
    return {{"HP-IN-ARROW drag placement endpoint handles and cancel",ArrowInteractions},
            {"HP-IN-TEXT font family size weight and rendering",TextFormatting},
            {"HP-IN-CANCEL part drag is restored by Escape",CancelPartDrag}};
}
}
