#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <TopoDS_Shape.hxx>
#include <TopExp_Explorer.hxx>
#include <STEPControl_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <StlAPI_Writer.hxx>
#include <Interface_Static.hxx>
#include <sstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <cmath>

std::string Text(QDataStream& s) {
    quint64 n;s>>n;if(n>512*1024*1024)throw std::runtime_error("oversized field");
    std::string result(n,'\0');if(s.readRawData(result.data(),int(n))!=qint64(n))throw std::runtime_error("short field");return result;
}
QJsonObject Json(const QString& p){QFile f(p);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("missing JSON");return QJsonDocument::fromJson(f.readAll()).object();}

TopoDS_Shape ReadShape(const std::map<std::string,std::string>& shapes,const QString& id) {
    const auto found=shapes.find(id.toStdString());
    if(found==shapes.end())throw std::runtime_error("missing CAD-evaluated shape");
    std::istringstream stream(found->second);BRep_Builder builder;TopoDS_Shape shape;
    BRepTools::Read(shape,stream,builder);return shape;
}

double Volume(const TopoDS_Shape& shape) {
    GProp_GProps prop;if(!shape.IsNull())BRepGProp::VolumeProperties(shape,prop,1e-10);
    return std::abs(prop.Mass());
}

QJsonArray CheckAssembly(const QJsonObject& design,const std::map<std::string,std::string>& shapes,int& failures) {
    QJsonArray results;
    for(const auto value:design["assemblyChecks"].toArray()) {
        auto check=value.toObject();const auto first=ReadShape(shapes,check["first"].toString());
        const auto second=ReadShape(shapes,check["second"].toString());
        BRepAlgoAPI_Common common(first,second);common.Build();
        if(!common.IsDone())throw std::runtime_error("assembly intersection failed");
        const double overlap=Volume(common.Shape());check["overlapMm3"]=overlap;
        check["passed"]=!check["noOverlap"].toBool()||overlap<=1e-6;
        if(!check["passed"].toBool())++failures;results.append(check);
    }
    return results;
}

QJsonArray CheckSplit(const QJsonObject& design,const std::map<std::string,std::string>& shapes,int& failures) {
    QJsonArray results;
    for(const auto value:design["splitChecks"].toArray()) {
        auto check=value.toObject();const auto source=ReadShape(shapes,check["source"].toString());
        const auto front=ReadShape(shapes,check["front"].toString());const auto rear=ReadShape(shapes,check["rear"].toString());
        BRepAlgoAPI_Fuse joined(front,rear);joined.Build();
        if(!joined.IsDone())throw std::runtime_error("split reassembly failed");
        BRepAlgoAPI_Cut missing(source,joined.Shape()),excess(joined.Shape(),source);
        if(!missing.IsDone()||!excess.IsDone())throw std::runtime_error("split comparison failed");
        const double error=Volume(missing.Shape())+Volume(excess.Shape());
        const double tolerance=std::max(1e-6,Volume(source)*1e-8);
        check["symmetricDifferenceMm3"]=error;check["toleranceMm3"]=tolerance;
        check["passed"]=BRepCheck_Analyzer(joined.Shape()).IsValid()&&error<=tolerance;
        if(!check["passed"].toBool())++failures;results.append(check);
    }
    return results;
}

QJsonArray CheckPoints(const QJsonObject& design,const std::map<std::string,std::string>& shapes,int& failures) {
    QJsonArray results;
    for(const auto value:design["pointChecks"].toArray()) {
        auto check=value.toObject();const auto point=check["point"].toArray();
        if(point.size()!=3||!check["expectedInside"].isBool())throw std::runtime_error("invalid point check");
        const auto shape=ReadShape(shapes,check["entity"].toString());
        BRepClass3d_SolidClassifier classifier(shape,gp_Pnt(point[0].toDouble(),point[1].toDouble(),point[2].toDouble()),1e-6);
        const bool inside=classifier.State()==TopAbs_IN;
        check["inside"]=inside;check["onBoundary"]=classifier.State()==TopAbs_ON;
        check["passed"]=classifier.State()!=TopAbs_UNKNOWN&&classifier.State()!=TopAbs_ON&&inside==check["expectedInside"].toBool();
        if(!check["passed"].toBool())++failures;results.append(check);
    }
    return results;
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);if(argc!=4&&argc!=5)return 2;
    const bool checkOnly=argc==5&&QString::fromLocal8Bit(argv[4])=="--check-only";
    if(argc==5&&!checkOnly)return 2;
    try {
        QFile file(QString::fromLocal8Bit(argv[1]));if(!file.open(QIODevice::ReadOnly))return 3;
        QDataStream data(&file);data.setVersion(QDataStream::Qt_6_0);data.setFloatingPointPrecision(QDataStream::DoublePrecision);
        if(Text(data)!="kachakacha-prepared-document")throw std::runtime_error("bad header");
        qint64 version;data>>version;if(version!=2)throw std::runtime_error("bad version");
        auto token=Text(data);auto archive=Text(data);
        quint64 count;data>>count;if(count>1000000)throw std::runtime_error("bad shape count");
        std::map<std::string,std::string> shapes;
        for(quint64 i=0;i<count;++i){auto id=Text(data);shapes.emplace(id,Text(data));}
        const auto design=Json(QString::fromLocal8Bit(argv[2]));const QDir out(QString::fromLocal8Bit(argv[3]));QDir().mkpath(out.path());
        const double chord=design["stlChordMm"].toDouble(.025);
        if(!std::isfinite(chord)||chord<=0||chord>.025)throw std::runtime_error("invalid mesh precision");
        if(design["entities"].toArray().isEmpty()||design["groups"].toArray().isEmpty())
            throw std::runtime_error("empty or invalid design manifest");
        std::map<QString,QJsonObject> groups;
        for(const auto value:design["groups"].toArray()){auto g=value.toObject();groups[g["id"].toString()]=g;}
        QJsonArray results;int failures=0,index=0;
        const auto assembly=CheckAssembly(design,shapes,failures);
        const auto split=CheckSplit(design,shapes,failures);
        const auto points=CheckPoints(design,shapes,failures);
        Interface_Static::SetCVal("write.step.unit","MM");
        for(const auto value:design["entities"].toArray()) {
            const auto entity=value.toObject();if(entity["kind"]!="part"||entity["visibility"]!="visible")continue;
            const auto id=entity["id"].toString();QJsonObject row{{"id",id},{"name",entity["displayName"]}};
            const double partChord=design["stlChordByEntity"].toObject()[id].toDouble(chord);
            if(!std::isfinite(partChord)||partChord<=0||partChord>.025)throw std::runtime_error("invalid part mesh precision");
            row["chordMm"]=partChord;
            const auto found=shapes.find(id.toStdString());
            if(found==shapes.end()){row["error"]="missing CAD-evaluated shape";results.append(row);++failures;continue;}
            std::istringstream stream(found->second);BRep_Builder builder;TopoDS_Shape shape;BRepTools::Read(shape,stream,builder);
            int solids=0;for(TopExp_Explorer ex(shape,TopAbs_SOLID);ex.More();ex.Next())++solids;
            GProp_GProps prop;const double sourceError=BRepGProp::VolumeProperties(shape,prop,1e-10);
            bool valid=!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid()&&solids==1&&prop.Mass()>1e-8;
            row["valid"]=valid;row["solids"]=solids;row["volumeMm3"]=prop.Mass();
            row["sourceIntegrationError"]=sourceError;
            Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);double a,b,c,d,e,f;box.Get(a,b,c,d,e,f);
            row["bounds"]=QJsonArray{a,b,c,d,e,f};
            const bool envelope=a>=-5&&b>=-21&&c>=-1&&d<=250&&e<=21&&f<=52;
            row["withinModelEnvelope"]=envelope;valid=valid&&envelope;row["valid"]=valid;
            QString group=entity["groupId"].toString();QStringList names;
            for(int depth=0;groups.contains(group)&&depth<50;++depth){auto g=groups[group];names.prepend(g["displayName"].toString());group=g["parentGroupId"].toString();}
            row["groupPath"]=names.join('/');row["manufacturing"]=entity["partProperties"].toObject()["manufacturing"];
            const auto stem=QString("part-%1-%2").arg(++index,3,10,QChar('0')).arg(id.left(8));
            if(valid&&!checkOnly) {
                BRepMesh_IncrementalMesh mesh(shape,partChord,false,.12,false);
                const auto stl=out.filePath(stem+".stl");StlAPI_Writer writer;writer.ASCIIMode()=false;
                const bool stlOk=writer.Write(shape,stl.toUtf8().constData());
                STEPControl_Writer step;const auto transferred=step.Transfer(shape,STEPControl_AsIs);
                const auto path=out.filePath(stem+".step");const auto written=transferred==IFSelect_RetDone?step.Write(path.toUtf8().constData()):IFSelect_RetError;
                row["stl"]=stem+".stl";row["step"]=stem+".step";row["exported"]=stlOk&&written==IFSelect_RetDone;
                STEPControl_Reader reader;
                bool roundtrip=written==IFSelect_RetDone&&reader.ReadFile(path.toUtf8().constData())==IFSelect_RetDone;
                if(roundtrip) {
                    reader.TransferRoots();const auto restored=reader.OneShape();GProp_GProps restoredProperties;
                    row["stepIntegrationError"]=BRepGProp::VolumeProperties(restored,restoredProperties,1e-10);
                    const double delta=std::abs(restoredProperties.Mass()-prop.Mass());row["stepVolumeDeltaMm3"]=delta;
                    roundtrip=BRepCheck_Analyzer(restored).IsValid()&&delta<=std::max(1e-5,std::abs(prop.Mass())*1e-7);
                }
                row["stepRoundTrip"]=roundtrip;if(!roundtrip)++failures;
                if(!row["exported"].toBool())++failures;
            }else if(!valid)++failures;
            results.append(row);
        }
        if(index==0)throw std::runtime_error("zero visible parts");
        QJsonObject report{{"preparedToken",QString::fromStdString(token)},{"cadShapes",int(count)},{"visibleParts",index},
            {"failures",failures},{"checkOnly",checkOnly},{"chordMm",chord},{"assemblyChecks",assembly},{"splitChecks",split},{"pointChecks",points},{"parts",results}};
        QFile result(out.filePath("print-parts.json"));if(!result.open(QIODevice::WriteOnly))return 4;result.write(QJsonDocument(report).toJson());
        std::cout<<"visible="<<index<<" failures="<<failures<<std::endl;return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 5;}
}
