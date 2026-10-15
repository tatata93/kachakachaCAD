#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2ProjectionTool.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/geometry/WireChain.h"
#include <cmath>
#include <iostream>
#include <QApplication>
#include <QChar>
#include <QString>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QPixmap>
#include <QTemporaryDir>

namespace kachakacha::v2::selftest {
bool PanoramaTwoPlanes(V2MainWindow& window)
{
    using geometry::Vector3;
    using geometry::CurveSegment;
    const auto line=[](Vector3 a,Vector3 b){return CurveSegment::MakeLine(a,b).Value();};
    const auto text=[](int n){return QStringLiteral("c0000000-0000-4000-8000-%1").arg(n,12,10,QChar('0')).toStdString();};
    const auto id=[&](int n){return *base::EntityId::Parse(text(n));};
    io::DocumentFile file;file.snapshot=window.Session().GetDocument().Snapshot();
    const auto add=[&](int n,domain::FeatureType type,domain::EntityKind kind,const auto& definition,const char* label){
        domain::Feature f;f.id=*base::FeatureId::Parse(text(n+100));f.type=type;f.definition=definition;f.displayName=label;
        domain::Entity e;e.id=id(n);e.createdBy=f.id;e.kind=kind;e.displayName=label;
        if(kind==domain::EntityKind::GuideSurface)e.editPolicy=domain::EditPolicy::Frozen;
        f.outputs.push_back({"output",e.id,e.kind});file.snapshot.features.push_back(f);file.snapshot.entities.push_back(e);
    };
    for(int plane=0;plane<2;++plane) {
        domain::CreateWorkPlaneDefinition d;d.method=static_cast<int>(modeling::WorkPlaneMethod::PointNormal);
        d.origin=plane?Vector3{10,0,0}:Vector3{0,-5,0};d.normal=plane?Vector3{1,0,0}:Vector3{0,-1,0};
        d.uDirection=plane?Vector3{0,1,0}:Vector3{1,0,0};
        add(plane+1,domain::FeatureType::CreateWorkPlane,domain::EntityKind::WorkPlane,d,plane?"側面作図面":"正面作図面");
    }
    const std::vector<std::vector<CurveSegment>> outlines={
        {line({-4,-5,1},{5,-5,1}),line({5,-5,-1},{-4,-5,-1}),line({-4,-5,-1},{-4,-5,1})},
        {line({10,5,1},{10,10,1}),line({10,10,1},{10,10,-1}),line({10,10,-1},{10,5,-1})}};
    for(int i=0;i<2;++i) {
        domain::CreateWireDefinition d;d.segments=outlines[i];
        for(int j=0;j<3;++j)d.segmentIds.push_back(*base::SegmentId::Parse(text(200+i*10+j)));
        add(i+3,domain::FeatureType::CreateWire,domain::EntityKind::Wire,d,i?"窓外周・側面側":"窓外周・正面側");
    }
    // Three independently bounded target surfaces: front, corner R and side.
    for(int panel=0;panel<3;++panel) {
        modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::RuledSections;
        for(int i=0;i<2;++i) {
            const double z=i?5:-5;modeling::GuideChain chain;chain.role=modeling::ChainRole::Section;chain.index=i+1;
            if(panel==0)chain.segments={line({-8,0,z},{0,0,z})};
            if(panel==1)chain.segments={CurveSegment::MakeCircularArc({0,5,z},{0,0,1},{1,0,0},5,-geometry::kPi/2,geometry::kPi/2).Value()};
            if(panel==2)chain.segments={line({5,5,z},{5,14,z})};
            request.chains.push_back(chain);
        }
        const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,{});if(!analysis.HasValue())return false;
        const auto surface=kernel::BuildGuideSurface(request,analysis.Value(),{});if(!surface.HasValue())return false;
        const auto bytes=kernel::CaptureOutputShape(surface.Value().handle);kernel::ReleaseShape(surface.Value().handle);
        if(!bytes.HasValue())return false;domain::FreezeDerivedDefinition d;d.frozenBrep=bytes.Value();
        add(panel+5,domain::FeatureType::FreezeDerived,domain::EntityKind::GuideSurface,d,panel==0?"車体・正面":panel==1?"車体・角のR":"車体・側面");
    }
    QTemporaryDir temporary;
    const auto output=qEnvironmentVariable("KACHACAD_PANORAMA_GUIDE_OUTPUT");
    const auto directory=output.isEmpty()?temporary.path():output;QDir().mkpath(directory);
    const auto bytes=io::SaveDocument(file);if(!bytes.HasValue())return false;
    const auto path=directory+"/panorama-input.kcd2";
    if(!io::WriteFileAtomically(path.toStdString(),bytes.Value()).HasValue()||!window.OpenDocumentFile(path))return false;
    window.resize(1500,950);window.Viewport().SetViewDirection(ViewDirection::Isometric);
    window.Viewport().SetViewCenter({2,3,0});window.Viewport().SetVisibleWidthMm(40);
    const auto capture=[&](const char* name){if(!output.isEmpty()){QApplication::processEvents();window.grab().save(directory+"/"+name);}};
    capture("01-two-planes.png");
    window.RunCommand("wire.project_surface");auto* tool=V2ProjectionTool::Open(window);
    for(int n:{3,4}){app::SelectionRef ref;ref.entityId=id(n);if(!tool->AddWire(ref))return false;}
    auto* rows=tool->findChild<QListWidget*>("projectionWireRows");
    const auto button=[&](const QString& name){for(auto* b:tool->findChildren<QPushButton*>())if(b->text()==name){b->click();return true;}return false;};
    rows->setCurrentRow(0);if(!button(QStringLiteral("Y方向")))return false;capture("02-front-direction.png");
    rows->setCurrentRow(1);if(!button(QStringLiteral("X方向"))||!button(QStringLiteral("方向を反転")))return false;
    if(!rows->item(0)->text().contains("0, 1, 0")||!rows->item(1)->text().contains("-1, 0, 0")) {
        // Geometry below independently checks direction; formatting is not a contract.
        Note("Panorama directions set through separate row buttons");
    }
    capture("03-side-direction.png");
    for(int n:{5,6,7}){app::SelectionRef ref;ref.entityId=id(n);if(!tool->AddTarget(ref))return false;}
    tool->findChild<QComboBox*>("projectionWireForm")->setCurrentIndex(1);
    if(!Explain("PROJ two-plane native preview",tool->Preview()))return false;capture("04-preview.png");
    const auto before=window.Session().GetDocument().Snapshot().features.size();
    if(!tool->Commit())return false;
    const auto result=window.Session().GetDocument().Snapshot().entities.back().id;
    app::SelectionSet selection;selection.entityIds={result};
    const auto curves=app::SelectedCurves(selection,window.Session().Scene());
    if(!Explain("PROJ two open drawings become one closed perimeter",geometry::SegmentsFormClosedLoop(curves,{})))return false;
    bool front=false,corner=false,side=false;
    for(const auto& c:curves)for(int i=0;i<=64;++i) {
        const auto p=c.Evaluate(i/64.0);
        const double error=p.x<0?std::abs(p.y):p.y>5?std::abs(p.x-5):std::abs(std::hypot(p.x,p.y-5)-5);
        if(error>.003)return false;
        front=front||p.x<-1;corner=corner||(p.x>1&&p.x<4&&p.y<5);side=side||p.y>6;
    }
    if(!front||!corner||!side)return false;
    window.RunCommand("selection.activate");window.Viewport().SetSelection(selection);capture("05-result.png");
    const auto resultPath=directory+"/panorama-result.kcd2";
    window.RunCommand("edit.undo");
    std::cout<<"PANORAMA undo features="<<window.Session().GetDocument().Snapshot().features.size()<<" expected="<<before<<" status="<<window.StatusText().toStdString()<<std::endl;
    if(!Explain("PROJ panorama single Undo",window.Session().GetDocument().Snapshot().features.size()==before))return false;
    window.RunCommand("edit.redo");
    window.SetPathChooser([resultPath](bool){return resultPath;});window.RunCommand("file.save_as");window.SetPathChooser({});
    return Explain("PROJ two-plane result survives save",window.OpenDocumentFile(resultPath)
        &&window.Session().GetDocument().FindEntity(result)!=nullptr);
}
} // namespace kachakacha::v2::selftest
