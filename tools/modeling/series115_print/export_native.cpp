#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QCryptographicHash>
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
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <BRepLib.hxx>
#include <GeomConvert.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <TopoDS.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <TopoDS_Compound.hxx>
#include <TopTools_ListOfShape.hxx>
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
#include <set>
#include <stdexcept>
#include <cmath>
#include <memory>

std::string Text(QDataStream& s) {
    quint64 n;s>>n;if(n>512*1024*1024)throw std::runtime_error("oversized field");
    std::string result(n,'\0');if(s.readRawData(result.data(),int(n))!=qint64(n))throw std::runtime_error("short field");return result;
}
QJsonObject Json(const QString& p){QFile f(p);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("missing JSON");return QJsonDocument::fromJson(f.readAll()).object();}
QString FileSha256(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("missing exported file");
    QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file))throw std::runtime_error("export hash read failed");return QString::fromLatin1(hash.result().toHex());}

TopoDS_Shape ReadShape(const std::map<std::string,std::string>& shapes,const QString& id) {
    const auto found=shapes.find(id.toStdString());
    if(found==shapes.end())throw std::runtime_error("missing CAD-evaluated shape");
    std::istringstream stream(found->second);BRep_Builder builder;TopoDS_Shape shape;
    BRepTools::Read(shape,stream,builder);return shape;
}

double Volume(const TopoDS_Shape& shape) {
    GProp_GProps prop;if(!shape.IsNull())BRepGProp::VolumeProperties(shape,prop,1e-10);
    if(!std::isfinite(prop.Mass()))throw std::runtime_error("non-finite volume");
    return std::abs(prop.Mass());
}

bool ClosedPositiveSolid(const TopoDS_Shape& shape){
    if(shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())return false;
    int count=0;for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next())++count;
    GProp_GProps props;BRepGProp::VolumeProperties(shape,props,1e-10);
    return count==1&&std::isfinite(props.Mass())&&props.Mass()>1e-8;
}

Bnd_Box InsertionFaceBounds(const TopoDS_Face& face){
    double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    if(!std::isfinite(u0)||!std::isfinite(u1)||!std::isfinite(v0)||!std::isfinite(v1)||u0>=u1||v0>=v1)
        throw std::runtime_error("unbounded insertion face");
    TopLoc_Location location;const auto surface=BRep_Tool::Surface(face,location);
    const auto patch=new Geom_RectangularTrimmedSurface(surface,u0,u1,v0,v1);
    const auto spline=GeomConvert::SurfaceToBSplineSurface(patch);Bnd_Box box;
    for(int u=1;u<=spline->NbUPoles();++u)for(int v=1;v<=spline->NbVPoles();++v){
        const double weight=spline->Weight(u,v);const auto point=spline->Pole(u,v).Transformed(location.Transformation());
        if(!std::isfinite(weight)||weight<=0||!std::isfinite(point.X())||!std::isfinite(point.Y())||!std::isfinite(point.Z()))
            throw std::runtime_error("invalid insertion control hull");box.Add(point);
    }
    const double tolerance=BRep_Tool::Tolerance(face);if(!std::isfinite(tolerance)||tolerance<0)throw std::runtime_error("invalid face tolerance");
    box.Enlarge(tolerance+1e-7);return box;
}

Bnd_Box InsertionSolidBounds(const TopoDS_Shape& shape){
    Bnd_Box box;for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next())box.Add(InsertionFaceBounds(TopoDS::Face(e.Current())));
    if(box.IsVoid())throw std::runtime_error("empty insertion control hull");return box;
}

QJsonObject TranslationGraph(const TopoDS_Face& face,const gp_Vec& delta,bool trimmed=false){
    QJsonObject result{{"accepted",false}};int axis=-1;
    for(int i=0;i<3;++i)if(delta.Coord(i+1)!=0){if(axis!=-1)return result;axis=i;}
    if(axis<0)return result;double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);TopLoc_Location location;
    const auto surface=BRep_Tool::Surface(face,location);auto s=trimmed?Handle(Geom_BSplineSurface){}:Handle(Geom_BSplineSurface)::DownCast(surface);
    if(s.IsNull()){const auto patch=new Geom_RectangularTrimmedSurface(surface,u0,u1,v0,v1);s=GeomConvert::SurfaceToBSplineSurface(patch);}
    if(s->IsUPeriodic()||s->IsVPeriodic())return result;const int nu=s->NbUPoles(),nv=s->NbVPoles();
    const int p=(axis+1)%3,q=(axis+2)%3;double rankError=0;bool uniformRows=true,uniformColumns=true;
    for(int i=1;i<=nu;++i)for(int j=1;j<=nv;++j){const double a=s->Weight(i,j)*s->Weight(1,1),b=s->Weight(i,1)*s->Weight(1,j);
        if(!std::isfinite(a)||a<=0||!std::isfinite(b)||b<=0)return result;rankError=std::max(rankError,std::abs(a-b)/std::max(a,b));
        uniformRows=uniformRows&&s->Weight(i,j)==s->Weight(1,j);uniformColumns=uniformColumns&&s->Weight(i,j)==s->Weight(i,1);}
    auto monotone=[](const std::vector<double>& values,int degree){
        int sign=0;for(std::size_t i=1;i<values.size();++i){const double difference=values[i]-values[i-1];if(std::abs(difference)<=1e-10)continue;
            const int current=difference>0?1:-1;if(sign&&sign!=current)return false;sign=current;}
        if(!sign)return false;
        for(std::size_t i=0;i+degree<values.size();++i)if(values[i+degree]==values[i])return false;
        return true;
    };
    for(int swap=0;swap<2;++swap){const int a=swap?q:p,b=swap?p:q;double crossA=0,crossB=0;std::vector<double> au,bv;
        for(int i=1;i<=nu;++i)au.push_back(s->Pole(i,1).Transformed(location.Transformation()).Coord(a+1));
        for(int j=1;j<=nv;++j)bv.push_back(s->Pole(1,j).Transformed(location.Transformation()).Coord(b+1));
        for(int i=1;i<=nu;++i)for(int j=1;j<=nv;++j){const auto point=s->Pole(i,j).Transformed(location.Transformation());
            crossA=std::max(crossA,std::abs(point.Coord(a+1)-au[i-1]));crossB=std::max(crossB,std::abs(point.Coord(b+1)-bv[j-1]));}
        // Numerical CAD gate, not a theorem about exact separability. The
        // measured residual is retained and is four orders below modelLinear.
        const bool accepted=(uniformRows||uniformColumns)&&crossA<=1e-10&&crossB<=1e-10&&monotone(au,s->UDegree())&&monotone(bv,s->VDegree());
        QJsonObject candidate{{"accepted",accepted},{"rankRelativeError",rankError},{"crossUMm",crossA},{"crossVMm",crossB},
            {"uPoles",nu},{"vPoles",nv},{"uDegree",s->UDegree()},{"vDegree",s->VDegree()},{"swapped",bool(swap)},
            {"exactUniformWeightRows",uniformRows},{"exactUniformWeightColumns",uniformColumns}};
        candidate["projectionResidualLimitMm"]=1e-10;
        candidate["scope"]="Numerical separability with recorded residual; not an exact mathematical injectivity proof";
        QJsonArray ua,va;for(const auto x:au)ua.append(x);for(const auto x:bv)va.append(x);candidate["uProjectedPoles"]=ua;candidate["vProjectedPoles"]=va;
        candidate["monotoneU"]=monotone(au,s->UDegree());candidate["monotoneV"]=monotone(bv,s->VDegree());
        if(accepted){gp_Vec ea(0,0,0),eb(0,0,0);ea.SetCoord(a+1,au.back()>au.front()?1.:-1.);eb.SetCoord(b+1,bv.back()>bv.front()?1.:-1.);
            candidate["expectedNormalSign"]=ea.Crossed(eb).Dot(gp_Dir(delta));}
        if(accepted||swap==1)result=candidate;if(accepted)break;
    }
    if(result["accepted"].toBool()){
        BRepAdaptor_Surface adaptor(face);gp_Pnt point;gp_Vec du,dv;adaptor.D1((u0+u1)/2,(v0+v1)/2,point,du,dv);
        const auto normal=du.Crossed(dv);const double cosine=normal.Magnitude()>1e-15?normal.Dot(gp_Dir(delta))/normal.Magnitude():0;
        result["centralProjectionCosine"]=cosine;
        if(!std::isfinite(cosine)||std::abs(cosine)<1e-8||cosine*result["expectedNormalSign"].toDouble()<=0)result["accepted"]=false;
    }
    result["trimmedParameterRectangle"]=trimmed;
    if(!result["accepted"].toBool()&&!trimmed)return TranslationGraph(face,delta,true);
    return result;
}

TopoDS_Shape InsertionFacePrism(const TopoDS_Face& face,const gp_Vec& delta,bool selfCheck){
    BRepPrimAPI_MakePrism sweep(face,delta,true,true);sweep.Build();
    if(!sweep.IsDone()||sweep.Shape().ShapeType()!=TopAbs_SOLID)return {};
    auto solid=TopoDS::Solid(sweep.Shape());if(!BRepLib::OrientClosedSolid(solid)||!ClosedPositiveSolid(solid))return {};
    if(selfCheck){BOPAlgo_ArgumentAnalyzer self;self.SetShape1(solid);self.ArgumentTypeMode()=false;self.SelfInterMode()=true;
        self.SmallEdgeMode()=false;self.RebuildFaceMode()=false;self.TangentMode()=false;self.MergeVertexMode()=false;
        self.MergeEdgeMode()=false;self.ContinuityMode()=false;self.CurveOnSurfaceMode()=false;self.StopOnFirstFaulty()=true;self.Perform();
        if(self.HasErrors()||self.HasFaulty())return {};}
    return solid;
}

TopoDS_Shape ContactIntersection(const TopoDS_Shape& a,const TopoDS_Shape& b);
double CurvedSweepUpperBound(const TopoDS_Face& face,const gp_Vec& delta,const TopoDS_Shape& obstacle,const Bnd_Box& obstacleBox,int depth,int& cells,const Bnd_Box* clip=nullptr,QJsonArray* graphProofs=nullptr){
    const auto box=InsertionFaceBounds(face);double low[3],high[3];box.Get(low[0],low[1],low[2],high[0],high[1],high[2]);
    if(clip){double a[3],b[3];clip->Get(a[0],a[1],a[2],b[0],b[1],b[2]);for(int i=0;i<3;++i){low[i]=std::max(low[i],a[i]);high[i]=std::min(high[i],b[i]);}}
    double start[3],end[3];for(int i=0;i<3;++i){start[i]=low[i]+std::min(0.,delta.Coord(i+1));end[i]=high[i]+std::max(0.,delta.Coord(i+1));}
    const auto containing=BRepPrimAPI_MakeBox(gp_Pnt(start[0],start[1],start[2]),gp_Pnt(end[0],end[1],end[2])).Shape();
    const double upper=Volume(ContactIntersection(containing,obstacle));++cells;
    if(upper<=1e-10||depth>=12)return upper;
    auto graph=TranslationGraph(face,delta);
    if(graph["accepted"].toBool()){
        const auto prism=InsertionFacePrism(face,delta,true);
        if(!prism.IsNull()){
            const auto common=ContactIntersection(prism,obstacle);if(!BRepCheck_Analyzer(common).IsValid())throw std::runtime_error("invalid graph prism intersection");
            graph["subdivisionDepth"]=depth;graph["occtSelfInterferenceChecked"]=true;if(graphProofs)graphProofs->append(graph);
            return std::min(upper,Volume(common));}
    }
    // Both clipping boxes together cover the complete control hull. Each
    // resulting trimmed face gets a new positive-weight control hull. Thus
    // refinement may reject a safe path but never samples away a collision.
    Bnd_Box tight;BRepBndLib::AddOptimal(face,tight,false,false);double l[3],h[3];tight.Get(l[0],l[1],l[2],h[0],h[1],h[2]);
    int axis=0;for(int i=1;i<3;++i)if(h[i]-l[i]>h[axis]-l[axis])axis=i;
    if(h[axis]-l[axis]<1e-6)return upper;double middle=(l[axis]+h[axis])/2;
    // Splitting at an obstacle boundary is only a refinement choice: BOTH
    // halves are intersected with the original face. No bounding estimate is
    // used to discard a part. Keep the clipping box as a containment invariant.
    double a0[3],b0[3];obstacleBox.Get(a0[0],a0[1],a0[2],b0[0],b0[1],b0[2]);
    for(int i=0;i<3;++i)if(delta.Coord(i+1)!=0){const double plane=std::round((delta.Coord(i+1)>0?b0[i]:a0[i])*1e6)/1e6;
        if(low[i]+1e-6<plane&&plane<high[i]-1e-6){axis=i;middle=plane;break;}}
    double result=0,area=0;GProp_GProps original;BRepGProp::SurfaceProperties(face,original,1e-10);int pieces=0;
    BRep_Builder builder;TopoDS_Compound covered;builder.MakeCompound(covered);
    for(int side=0;side<2;++side){double a[3],b[3];for(int i=0;i<3;++i){a[i]=low[i]-1e-6;b[i]=high[i]+1e-6;}
        if(clip){double aa[3],bb[3];clip->Get(aa[0],aa[1],aa[2],bb[0],bb[1],bb[2]);for(int i=0;i<3;++i){a[i]=std::max(a[i],aa[i]);b[i]=std::min(b[i],bb[i]);}}
        if(side==0)b[axis]=middle;else a[axis]=middle;
        const auto tool=BRepPrimAPI_MakeBox(gp_Pnt(a[0],a[1],a[2]),gp_Pnt(b[0],b[1],b[2])).Shape();
        Bnd_Box childClip;childClip.Update(a[0],a[1],a[2],b[0],b[1],b[2]);
        const auto part=ContactIntersection(face,tool);if(!BRepCheck_Analyzer(part).IsValid())throw std::runtime_error("invalid insertion face subdivision");
        builder.Add(covered,part);
        for(TopExp_Explorer e(part,TopAbs_FACE);e.More();e.Next()){
            GProp_GProps props;BRepGProp::SurfaceProperties(e.Current(),props,1e-10);area+=std::abs(props.Mass());++pieces;
            result+=CurvedSweepUpperBound(TopoDS::Face(e.Current()),delta,obstacle,obstacleBox,depth+1,cells,&childClip,graphProofs);}
    }
    // Area agreement cannot certify complete coverage: a small missing area
    // can sweep a collision volume above the independent volume tolerance.
    BRepAlgoAPI_Cut missing(face,covered);missing.Build();bool complete=pieces>0&&std::isfinite(area)&&missing.IsDone()&&BRepCheck_Analyzer(missing.Shape()).IsValid();
    if(complete){TopExp_Explorer remainder(missing.Shape(),TopAbs_FACE);complete=!remainder.More();}
    if(!complete){std::cerr<<"insertion-refinement retained coarse bound: depth="<<depth<<" pieces="<<pieces<<" original-area="<<original.Mass()<<" child-area="<<area<<" upper="<<upper<<std::endl;return upper;}
    return std::min(upper,result);
}

TopoDS_Shape ContactIntersection(const TopoDS_Shape& a,const TopoDS_Shape& b){
    BRepAlgoAPI_Common operation;TopTools_ListOfShape args,tools;args.Append(a);tools.Append(b);
    operation.SetArguments(args);operation.SetTools(tools);operation.SetNonDestructive(true);operation.Build();
    if(!operation.IsDone())throw std::runtime_error("contact intersection failed");return operation.Shape();
}
TopoDS_Shape ContactShape(const QJsonObject& check,const QString& key,const std::map<std::string,std::string>& shapes){
    if(!check.contains(key+"Parts"))return ReadShape(shapes,check[key].toString());
    const auto ids=check[key+"Parts"].toArray();if(ids.isEmpty())throw std::runtime_error("empty contact assembly");
    TopoDS_Shape result;std::set<QString> unique;
    for(const auto id:ids){if(!unique.insert(id.toString()).second)throw std::runtime_error("duplicate contact part");
        const auto part=ReadShape(shapes,id.toString());if(result.IsNull())result=part;
        else {BRepAlgoAPI_Fuse join(result,part);join.Build();if(!join.IsDone())throw std::runtime_error("contact assembly failed");
            join.SimplifyResult();result=join.Shape();}}
    return result;
}
struct ContactFace {TopoDS_Shape shape;double b[6];};
std::vector<ContactFace> ContactFaces(const TopoDS_Shape& shape){
    std::vector<ContactFace> faces;
    for(TopExp_Explorer ex(shape,TopAbs_FACE);ex.More();ex.Next()){
        ContactFace face;face.shape=ex.Current();Bnd_Box box;BRepBndLib::AddOptimal(face.shape,box,false,false);
        if(box.IsVoid())continue;box.Get(face.b[0],face.b[1],face.b[2],face.b[3],face.b[4],face.b[5]);faces.push_back(face);}
    return faces;
}
QJsonArray CheckContacts(const QJsonObject& design,const std::map<std::string,std::string>& shapes,int& failures){
    QJsonArray results;
    for(const auto value:design["attachmentChecks"].toArray()){
        auto check=value.toObject();const double maximum=check["maximumDistanceMm"].toDouble(-1),minimum=check["minimumAreaMm2"].toDouble(-1);
        if(!std::isfinite(maximum)||maximum<=0||!std::isfinite(minimum)||minimum<=0)throw std::runtime_error("invalid contact limits");
        auto a=ContactShape(check,"first",shapes),b=ContactShape(check,"second",shapes);
        if(check.contains("secondTranslationMm")){const auto p=check["secondTranslationMm"].toArray();if(p.size()!=3)throw std::runtime_error("invalid contact perturbation");
            gp_Trsf t;t.SetTranslation(gp_Vec(p[0].toDouble(),p[1].toDouble(),p[2].toDouble()));b=BRepBuilderAPI_Transform(b,t,true).Shape();}
        const auto left=ContactFaces(a),right=ContactFaces(b);TopoDS_Shape region;
        if(check.contains("region")){const auto r=check["region"].toArray();if(r.size()!=6)throw std::runtime_error("invalid contact region");
            for(int i=0;i<3;++i)if(!std::isfinite(r[i].toDouble())||!std::isfinite(r[i+3].toDouble())||r[i+3].toDouble()<=r[i].toDouble())throw std::runtime_error("invalid contact box");
            region=BRepPrimAPI_MakeBox(gp_Pnt(r[0].toDouble(),r[1].toDouble(),r[2].toDouble()),gp_Pnt(r[3].toDouble(),r[4].toDouble(),r[5].toDouble())).Shape();
            a=ContactIntersection(a,region);b=ContactIntersection(b,region);}
        BRepExtrema_DistShapeShape distance(a,b);double gap=1e30,area=0;
        if(distance.IsDone()&&distance.NbSolution()>0)gap=distance.Value();
        if(gap<=maximum){
            for(const auto& f:left)for(const auto& g:right){bool close=true;
                for(int i=0;i<3;++i)if(std::min(f.b[i+3],g.b[i+3])-std::max(f.b[i],g.b[i]) < -maximum){close=false;break;}
                if(!close)continue;auto surface=ContactIntersection(f.shape,g.shape);
                if(!region.IsNull())surface=ContactIntersection(surface,region);
                GProp_GProps props;BRepGProp::SurfaceProperties(surface,props);area+=std::abs(props.Mass());}}
        check["distanceMm"]=gap;check["contactAreaMm2"]=area;check["passed"]=gap<=maximum&&area>=minimum;
        if(!check["passed"].toBool())++failures;results.append(check);
    }return results;
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
        auto parts=check.contains("parts")?check["parts"].toArray():QJsonArray{check["front"],check["rear"]};
        if(parts.size()<2)throw std::runtime_error("split must contain at least two pieces");
        if(!ClosedPositiveSolid(source))throw std::runtime_error("invalid split source solid");
        std::set<QString> ids;TopoDS_Shape joined;std::vector<TopoDS_Shape> pieces;double overlap=0;
        for(const auto id:parts){if(!id.isString()||id.toString().isEmpty()||!ids.insert(id.toString()).second)throw std::runtime_error("invalid split piece");
            const auto part=ReadShape(shapes,id.toString());if(!ClosedPositiveSolid(part))throw std::runtime_error("invalid split piece solid");
            for(const auto& other:pieces)overlap+=Volume(ContactIntersection(part,other));pieces.push_back(part);
            if(joined.IsNull())joined=part;else{
                BRepAlgoAPI_Fuse fuse(joined,part);fuse.Build();if(!fuse.IsDone())throw std::runtime_error("split reassembly failed");joined=fuse.Shape();}}
        BRepAlgoAPI_Cut missing(source,joined),excess(joined,source);
        if(!missing.IsDone()||!excess.IsDone())throw std::runtime_error("split comparison failed");
        const double error=Volume(missing.Shape())+Volume(excess.Shape());
        const double tolerance=std::max(1e-6,Volume(source)*1e-8);
        check["symmetricDifferenceMm3"]=error;check["toleranceMm3"]=tolerance;
        check["pieceOverlapSumMm3"]=overlap;
        check["passed"]=BRepCheck_Analyzer(joined).IsValid()&&error<=tolerance&&overlap<=1e-6;
        if(!check["passed"].toBool())++failures;results.append(check);
    }
    return results;
}

QJsonArray CheckInsertions(const QJsonObject& design,const std::map<std::string,std::string>& shapes,int& failures){
    QJsonArray results;
    for(const auto value:design["insertionChecks"].toArray()){
        auto check=value.toObject();const auto vector=check["vectorMm"].toArray();
        if(vector.size()!=3||!vector[0].isDouble()||!vector[1].isDouble()||!vector[2].isDouble())throw std::runtime_error("invalid insertion vector");
        const gp_Vec delta(vector[0].toDouble(),vector[1].toDouble(),vector[2].toDouble());
        if(!std::isfinite(delta.Magnitude())||delta.Magnitude()<1e-6)throw std::runtime_error("empty insertion path");
        const auto moving=check["moving"].toArray(),fixed=check["fixed"].toArray();
        if(moving.isEmpty()||fixed.isEmpty())throw std::runtime_error("empty insertion bodies");
        double maximum=0,upperSum=0;int faces=0,skipped=0,curvedBounds=0,cells=0,graphPrisms=0;bool valid=true;QJsonArray issues,graphProofs;
        for(const auto a:moving)for(const auto b:fixed){const auto source=ReadShape(shapes,a.toString()),obstacle=ReadShape(shapes,b.toString());
            if(!ClosedPositiveSolid(source)||!ClosedPositiveSolid(obstacle))throw std::runtime_error("invalid insertion reference solid");
            BRepAlgoAPI_Common initial(source,obstacle);initial.Build();if(!initial.IsDone())throw std::runtime_error("insertion initial intersection failed");
            const double initialVolume=Volume(initial.Shape());maximum=std::max(maximum,initialVolume);upperSum+=initialVolume;
            bool obstructed=initialVolume>1e-6;
            // Samples may establish a failure early. They never establish a
            // successful continuous path or replace the containing-volume pass.
            for(int position=1;position<=16&&!obstructed;++position){
                gp_Trsf t;t.SetTranslation(delta*(double(position)/16));
                const auto placed=BRepBuilderAPI_Transform(source,t,true).Shape();const double volume=Volume(ContactIntersection(placed,obstacle));
                if(volume>1e-6){maximum=std::max(maximum,volume);upperSum+=volume;obstructed=true;
                    issues.append(QJsonObject{{"moving",a},{"fixed",b},{"collisionAtFraction",double(position)/16},{"overlapMm3",volume}});}
            }
            if(obstructed){valid=false;continue;}
            const Bnd_Box obstacleBounds=InsertionSolidBounds(obstacle);
            double low[3],high[3];obstacleBounds.Get(low[0],low[1],low[2],high[0],high[1],high[2]);
            int pairFaces=0;
            for(TopExp_Explorer e(source,TopAbs_FACE);e.More();e.Next()){
                const auto face=TopoDS::Face(e.Current());++faces;++pairFaces;const Bnd_Box box=InsertionFaceBounds(face);
                double l[3],h[3];box.Get(l[0],l[1],l[2],h[0],h[1],h[2]);bool separated=false;
                for(int i=0;i<3;++i)if(h[i]+std::max(0.,delta.Coord(i+1))<low[i]-1e-7||l[i]+std::min(0.,delta.Coord(i+1))>high[i]+1e-7)separated=true;
                if(separated){++skipped;continue;}
                BRepAdaptor_Surface surface(face);const gp_Dir direction(delta);
                if(surface.GetType()==GeomAbs_Plane&&surface.Plane().Axis().Direction().Dot(direction)==0.){++skipped;continue;}
                TopoDS_Shape occupied;double overlap=0;
                const auto graph=surface.GetType()==GeomAbs_Plane?QJsonObject{}:TranslationGraph(face,delta);
                if(surface.GetType()==GeomAbs_Plane||graph["accepted"].toBool()){
                    occupied=InsertionFacePrism(face,delta,graph["accepted"].toBool());const bool usable=!occupied.IsNull();
                    if(usable){const auto common=ContactIntersection(occupied,obstacle);
                        if(!BRepCheck_Analyzer(common).IsValid()){valid=false;issues.append("invalid prism intersection");continue;}overlap=Volume(common);
                        if(graph["accepted"].toBool()){++graphPrisms;auto row=graph;row["moving"]=a;row["fixed"]=b;row["face"]=pairFaces;graphProofs.append(row);}
                    }else{overlap=CurvedSweepUpperBound(face,delta,obstacle,obstacleBounds,0,cells,nullptr,&graphProofs);++curvedBounds;}
                }else{
                    // A containing box gives an upper bound even for folded,
                    // non-injective or numerically incomplete curved prisms.
                    // A positive result may reject a safe path; it cannot be
                    // used to claim that an uncompleted curved sweep passed.
                    overlap=CurvedSweepUpperBound(face,delta,obstacle,obstacleBounds,0,cells,nullptr,&graphProofs);++curvedBounds;
                }
                if(overlap>1e-7)issues.append(QJsonObject{{"moving",a},{"fixed",b},{"face",pairFaces},{"surfaceType",int(surface.GetType())},{"upperMm3",overlap},{"graph",TranslationGraph(face,delta)}});
                maximum=std::max(maximum,overlap);upperSum+=overlap;
            }
            if(pairFaces==0)valid=false;
        }
        check["method"]="Numerical CAD gate: initial volume plus planar/numerically separable monotone graph boundary prisms with OCCT self-interference checks; other faces use swept control-hull AABBs. Not an exact-set injectivity proof";
        check["boundaryFaces"]=faces;check["separatedOrZeroVolumeFaces"]=skipped;check["maximumOverlapMm3"]=maximum;
        check["curvedContainingBoxes"]=curvedBounds;check["overlapUpperBoundMm3"]=upperSum;
        check["containingCells"]=cells;check["issues"]=issues;
        check["graphPrismFaces"]=graphProofs.size();check["graphProofs"]=graphProofs;
        check["complete"]=valid&&faces>0&&std::isfinite(upperSum);check["passed"]=check["complete"].toBool()&&upperSum<=1e-6;
        if(!check["passed"].toBool())++failures;results.append(check);
    }return results;
}

QJsonObject PointRayDiagnostic(const TopoDS_Shape& shape,const gp_Pnt& point,
    IntCurvesFace_ShapeIntersector* preparedRay=nullptr,const Bnd_Box* preparedBounds=nullptr){
    Bnd_Box bounds;if(preparedBounds)bounds=*preparedBounds;else BRepBndLib::AddOptimal(shape,bounds,false,false);
    double lo[3],hi[3];bounds.Get(lo[0],lo[1],lo[2],hi[0],hi[1],hi[2]);
    IntCurvesFace_ShapeIntersector localRay;if(!preparedRay)localRay.Load(shape,1e-7);
    auto& ray=preparedRay?*preparedRay:localRay;
    ray.Perform(gp_Lin(gp_Pnt(lo[0]-1,point.Y(),point.Z()),gp_Dir(1,0,0)),0,hi[0]-lo[0]+2);
    if(ray.IsDone())ray.SortResult();QJsonArray hits;bool usable=ray.IsDone(),inside=false;double last=-1e300;
    if(ray.IsDone())for(int i=1;i<=ray.NbPnt();++i){const double x=ray.Pnt(i).X();
        hits.append(QJsonObject{{"x",x},{"state",int(ray.State(i))},{"transition",int(ray.Transition(i))}});
        if(ray.State(i)!=TopAbs_IN||x-last<=1e-6||int(ray.Transition(i))!=(i%2?1:2))usable=false;
        if(x<point.X())inside=!inside;last=x;}
    if(hits.size()%2)usable=false;
    return QJsonObject{{"rayDone",ray.IsDone()},{"fixedXRay",hits},{"rayUsable",usable},{"rayInside",inside}};
}

QJsonObject PointBoundaryDiagnostic(const TopoDS_Shape& shape,const gp_Pnt& point){
    BRep_Builder builder;TopoDS_Compound faces;builder.MakeCompound(faces);
    for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next())builder.Add(faces,e.Current());
    BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Shape(),faces);
    QJsonObject result=PointRayDiagnostic(shape,point);QJsonArray nearest;
    if(distance.IsDone()){
        result["boundaryDistanceMm"]=distance.Value();
        for(int i=1;i<=distance.NbSolution();++i){
            QJsonObject row;const auto support=distance.SupportOnShape2(i);const auto q=distance.PointOnShape2(i);
            row["point"]=QJsonArray{q.X(),q.Y(),q.Z()};row["supportType"]=int(distance.SupportTypeShape2(i));
            if(distance.SupportTypeShape2(i)==BRepExtrema_IsInFace){
                double u,v;distance.ParOnFaceS2(i,u,v);gp_Pnt p;gp_Vec du,dv;
                const auto face=TopoDS::Face(support);BRepAdaptor_Surface surface(face);surface.D1(u,v,p,du,dv);
                row["c1Surface"]=surface.UContinuity()>=GeomAbs_C1&&surface.VContinuity()>=GeomAbs_C1;
                gp_Vec normal=du.Crossed(dv);if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
                if(normal.Magnitude()>1e-12){normal.Normalize();row["outwardDotMm"]=normal.Dot(gp_Vec(q,point));}
                row["uv"]=QJsonArray{u,v};row["reversed"]=face.Orientation()==TopAbs_REVERSED;
            }nearest.append(row);
        }
    }
    result["nearest"]=nearest;
    // A smooth, unique nearest face and an independent, non-tangent BRep ray
    // must agree. Do not rescue rejected classifiers, edge/tangent ambiguity,
    // on-boundary probes, or a merely matching STL/expected answer.
    bool usable=result["rayUsable"].toBool()&&nearest.size()==1&&distance.IsDone()&&distance.Value()>2e-5;
    const bool rayInside=result["rayInside"].toBool();bool conflict=false;
    if(usable){const auto face=nearest[0].toObject();const double dot=face["outwardDotMm"].toDouble(1e300);
        if(!face["c1Surface"].toBool()||!face.contains("outwardDotMm")||std::abs(std::abs(dot)-distance.Value())>1e-6)usable=false;
        if(usable&&(dot<0)!=rayInside){conflict=true;usable=false;}
        if(usable)result["independentInside"]=rayInside;}
    result["independentUsable"]=usable;result["independentConflict"]=conflict;return result;
}

QJsonArray CheckPoints(const QJsonObject& design,const std::map<std::string,std::string>& shapes,int& failures) {
    QJsonArray results;std::map<QString,TopoDS_Shape> loaded;std::map<QString,Bnd_Box> bounds;
    std::map<QString,std::unique_ptr<IntCurvesFace_ShapeIntersector>> rays;
    std::map<QString,std::unique_ptr<BRepClass3d_SolidClassifier>> classifiers;
    for(const auto value:design["pointChecks"].toArray()) {
        auto check=value.toObject();const auto point=check["point"].toArray();
        if(point.size()!=3||!check["expectedInside"].isBool())throw std::runtime_error("invalid point check");
        const auto id=check["entity"].toString();
        if(!loaded.count(id)){
            loaded[id]=ReadShape(shapes,id);BRepBndLib::AddOptimal(loaded[id],bounds[id],false,false);
            rays[id]=std::make_unique<IntCurvesFace_ShapeIntersector>();rays[id]->Load(loaded[id],1e-7);
            classifiers[id]=std::make_unique<BRepClass3d_SolidClassifier>(loaded[id]);
        }
        const auto& shape=loaded[id];auto& classifier=*classifiers[id];
        classifier.Perform(gp_Pnt(point[0].toDouble(),point[1].toDouble(),point[2].toDouble()),1e-6);
        const bool inside=classifier.State()==TopAbs_IN;
        check["rawInside"]=inside;check["onBoundary"]=classifier.State()==TopAbs_ON;
        check["rawState"]=int(classifier.State());check["rejected"]=classifier.Rejected();
        const gp_Pnt position(point[0].toDouble(),point[1].toDouble(),point[2].toDouble());
        auto diagnostic=PointRayDiagnostic(shape,position,rays[id].get(),&bounds[id]);
        const bool disagreement=diagnostic["rayUsable"].toBool()&&diagnostic["rayInside"].toBool()!=inside;
        if(disagreement)diagnostic=PointBoundaryDiagnostic(shape,position);
        const bool independent=diagnostic["independentUsable"].toBool();
        const bool verifiedInside=independent?diagnostic["independentInside"].toBool():inside;
        check["inside"]=verifiedInside;check["independentUsable"]=independent;
        check["classificationDisagreement"]=independent&&verifiedInside!=inside;
        check["fixedRayUsable"]=diagnostic["rayUsable"].toBool();
        check["passed"]=!classifier.Rejected()&&!diagnostic["independentConflict"].toBool()&&(!disagreement||independent)&&classifier.State()!=TopAbs_UNKNOWN&&classifier.State()!=TopAbs_ON&&verifiedInside==check["expectedInside"].toBool();
        if(!check["passed"].toBool()||disagreement)check["boundaryDiagnostic"]=diagnostic;
        if(!check["passed"].toBool())++failures;results.append(check);
    }
    return results;
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);if(argc!=4&&argc!=5)return 2;
    const bool checkOnly=argc==5&&QString::fromLocal8Bit(argv[4])=="--check-only";
    const bool previewOnly=argc==5&&QString::fromLocal8Bit(argv[4])=="--preview-only";
    const bool graphAudit=argc==5&&QString::fromLocal8Bit(argv[4])=="--graph-audit";
    if(argc==5&&!checkOnly&&!previewOnly&&!graphAudit)return 2;
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
        if(graphAudit){QJsonArray rows;for(const auto v:design["insertionChecks"].toArray()){
            const auto check=v.toObject();const auto vector=check["vectorMm"].toArray();if(vector.size()!=3)throw std::runtime_error("invalid audit vector");
            const gp_Vec delta(vector[0].toDouble(),vector[1].toDouble(),vector[2].toDouble());
            for(const auto id:check["moving"].toArray()){const auto source=ReadShape(shapes,id.toString());int index=0;
                for(TopExp_Explorer e(source,TopAbs_FACE);e.More();e.Next()){
                    const auto face=TopoDS::Face(e.Current());auto row=TranslationGraph(face,delta);row["moving"]=id;row["face"]=++index;
                    row["surfaceType"]=int(BRepAdaptor_Surface(face).GetType());rows.append(row);}}}
            QFile f(out.filePath("graph-audit.json"));if(!f.open(QIODevice::WriteOnly))return 4;
            f.write(QJsonDocument(QJsonObject{{"scope","Control-net diagnostics only; no successful path proof"},{"preparedToken",QString::fromStdString(token)},{"faces",rows}}).toJson());
            std::cout<<"DIAGNOSTICS ONLY"<<std::endl;return 0;}
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
        const auto contacts=CheckContacts(design,shapes,failures);
        const auto insertions=CheckInsertions(design,shapes,failures);
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
            if(solids!=1){QJsonArray fragments;for(TopExp_Explorer fragment(shape,TopAbs_SOLID);fragment.More();fragment.Next()){
                Bnd_Box bounds;BRepBndLib::AddOptimal(fragment.Current(),bounds,false,false);double l[3],h[3];bounds.Get(l[0],l[1],l[2],h[0],h[1],h[2]);
                fragments.append(QJsonObject{{"volumeMm3",Volume(fragment.Current())},{"bounds",QJsonArray{l[0],l[1],l[2],h[0],h[1],h[2]}}});}row["solidFragments"]=fragments;}
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
                row["stl"]=stem+".stl";
                if(previewOnly){
                    // Intermediate visual inspection only. A final deliverable
                    // must still use the unchanged full STEP round-trip path.
                    row["exported"]=stlOk;row["stepRoundTrip"]=false;
                    if(!stlOk)++failures;results.append(row);continue;
                }
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
                if(row["exported"].toBool()&&roundtrip){row["stlSha256"]=FileSha256(stl);row["stepSha256"]=FileSha256(path);}
            }else if(!valid)++failures;
            results.append(row);
        }
        if(index==0)throw std::runtime_error("zero visible parts");
        QJsonObject report{{"preparedToken",QString::fromStdString(token)},{"cadShapes",int(count)},{"visibleParts",index},
            {"failures",failures},{"checkOnly",checkOnly},{"previewOnly",previewOnly},{"chordMm",chord},{"assemblyChecks",assembly},{"splitChecks",split},{"pointChecks",points},{"attachmentChecks",contacts},{"insertionChecks",insertions},{"parts",results}};
        QFile result(out.filePath("print-parts.json"));if(!result.open(QIODevice::WriteOnly))return 4;result.write(QJsonDocument(report).toJson());
        std::cout<<"visible="<<index<<" failures="<<failures<<std::endl;return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 5;}
}
