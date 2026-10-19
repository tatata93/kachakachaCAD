#include "V2SelfTest.h"
#include "V2InstructionSheet.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QWidget>
#include <iostream>
#include <memory>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {
std::vector<InstructionPage> Scenes() {
    auto asset=std::make_shared<InstructionAsset>();asset->name=QStringLiteral("描画確認用の板");
    modeling::MeshTriangle a,b;a.points={geometry::Vector3{-10,-10,0},{10,-10,0},{10,10,0}};
    b.points={geometry::Vector3{-10,-10,0},{10,10,0},{-10,10,0}};a.normal=b.normal={0,0,1};
    asset->mesh.triangles={a,b};asset->mesh.edges={{{-10,-10,0},{10,-10,0},{10,10,0},{-10,10,0},{-10,-10,0}}};
    modeling::RefreshBounds(asset->mesh);InstructionPart part;part.asset=asset;
    InstructionPage scene;scene.title=QStringLiteral("描画確認");scene.parts={part};scene.span=40;
    return {scene};
}
bool Compare(const QString& name,const QImage& image) {
    const auto reference=qEnvironmentVariable("KACHACAD_INSTRUCTION_PAINT_REFERENCE");
    if(reference.isEmpty())return true;
    QDir dir(reference);dir.mkpath(".");const auto path=dir.filePath(name+".png");
    if(qEnvironmentVariableIsSet("KACHACAD_INSTRUCTION_PAINT_BASELINE"))return image.save(path);
    return Explain("optimized painting is pixel identical to reference painting",QImage(path)==image);
}
}
bool InstructionPaintPerformance(V2MainWindow&) {
    auto scenes=Scenes();const bool baseline=qEnvironmentVariableIsSet("KACHACAD_INSTRUCTION_PAINT_BASELINE");
    for(bool outside:{false,true}) {
        InstructionSheet sheet;InstructionSheetItem item;item.sceneId=scenes[0].id;
        item.rectMm=QRectF(outside?10000:60,outside?10000:100,160,160);sheet.items={item};
        V2InstructionSheetView view;view.resize(900,700);view.SetDocument(&sheet,&scenes);view.SetZoom(8);
        QElapsedTimer timer;timer.start();const auto first=view.Render(view.size(),true);const auto cold=timer.elapsed();
        const auto count=view.SceneRenderCount();timer.restart();const auto second=view.Render(view.size(),true);const auto warm=timer.elapsed();
        std::cout<<"instruction paint outside="<<outside<<", first="<<cold<<" ms, repeat="<<warm
            <<" ms, scene builds="<<count<<" -> "<<view.SceneRenderCount()<<", cache bytes="<<view.SceneCacheBytes()<<std::endl;
        if(!Explain("a repeated paper paint is pixel identical",first==second))return false;
        if(!Compare(outside?"outside":"visible",second))return false;
        if(!baseline&&!Explain("offscreen scenes are not rendered and a visible large scene is reused",outside?
            view.SceneRenderCount()==0:(count==1&&view.SceneRenderCount()==1)))return false;
        if(!Explain("paper scene image cache remains bounded",view.SceneCacheBytes()<=128*1024*1024))return false;
        const auto physical=view.Render({840,1188},false);
        if(!Compare(outside?"outside-export":"visible-export",physical))return false;
        view.SetZoom(1);
        if(!Explain("view navigation never changes physical export pixels",view.Render({840,1188},false)==physical))return false;
    }
    return true;
}
}
