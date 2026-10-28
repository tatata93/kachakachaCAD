#include "V2PreparedDocument.h"
#include "kachakacha/app/GeometryJobDiagnostic.h"
#include <QDataStream>
#include <QFile>
#include <QIODevice>
#include <QSaveFile>
#include <QString>
#include <QtGlobal>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace kachakacha::v2;
template<class T> struct IsVector:std::false_type {};
template<class T> struct IsVector<std::vector<T>>:std::true_type {};
template<class T> struct IsOptional:std::false_type {};
template<class T> struct IsOptional<std::optional<T>>:std::true_type {};
template<class T> struct IsMap:std::false_type {};
template<class K,class V> struct IsMap<std::map<K,V>>:std::true_type {};
template<class T> T EmptyValue(){return T{};}
template<> geometry::CurveSegment EmptyValue<geometry::CurveSegment>() {
    return geometry::CurveSegment::MakeLine({0,0,0},{1,0,0}).Value();
}
class Archive {
public:
    Archive(QIODevice* device,bool reading,std::function<bool()> pulse={}):stream(device),read(reading),pulse_(std::move(pulse)) {
        stream.setVersion(QDataStream::Qt_6_0);stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
    }
    template<class... T> void Values(T&... values);
    template<class T> void Field(T& value);
    std::size_t Count(std::size_t n,std::size_t limit=1000000) {
        quint64 count=n;if(read)stream>>count;else stream<<count;
        Check();if(count>limit)throw std::runtime_error("Collection exceeds transfer limit");
        return static_cast<std::size_t>(count);
    }
    void Check() {
        if(stream.status()!=QDataStream::Ok)throw std::runtime_error("Incomplete calculation result");
        if(++ticks_%4096==0&&pulse_&&!pulse_())throw std::runtime_error("Cancelled while reading calculation result");
    }
    QDataStream stream;bool read;
    std::size_t remainingElements=16000000;
private:
    std::function<bool()> pulse_;std::size_t ticks_=0;
};
void Fields(Archive& a,geometry::Vector3& v){a.Values(v.x,v.y,v.z);}
void Fields(Archive& a,geometry::Point2& v){a.Values(v.u,v.v);}
void Fields(Archive& a,base::EntityId& id) {
    auto text=id.ToString();a.Values(text);if(!a.read)return;
    const auto parsed=base::EntityId::Parse(text);if(!parsed)throw std::runtime_error("Invalid result entity ID");id=*parsed;
}
void Fields(Archive& a,geometry::CurveSegment& curve) {
    auto kind=curve.Kind();auto points=curve.ControlPoints();auto center=curve.Center();auto normal=curve.Normal();
    auto reference=curve.ReferenceDirection();auto radius=curve.Radius();auto start=curve.StartAngleRad();auto sweep=curve.SweepAngleRad();
    a.Values(kind,points,center,normal,reference,radius,start,sweep);if(!a.read)return;
    const auto made=[&]()->base::Result<geometry::CurveSegment>{
        switch(kind) {
        case geometry::CurveKind::Line:
            if(points.size()==2)return geometry::CurveSegment::MakeLine(points[0],points[1]);break;
        case geometry::CurveKind::CircularArc:return geometry::CurveSegment::MakeCircularArc(center,normal,reference,radius,start,sweep);
        case geometry::CurveKind::Circle:return geometry::CurveSegment::MakeCircle(center,normal,reference,radius);
        case geometry::CurveKind::CubicBezier:return geometry::CurveSegment::MakeCubicBezier(points);
        case geometry::CurveKind::CubicBSpline:return geometry::CurveSegment::MakeCubicBSpline(points);
        }
        throw std::runtime_error("Invalid result curve kind");
    }();
    if(!made.HasValue())throw std::runtime_error("Invalid curve in calculation result");curve=made.Value();
}
void Fields(Archive& a,fabrication::SurfacePatchSamples& v){a.Values(v.points,v.rowCount,v.columnCount);}
void Fields(Archive& a,fabrication::PatternPanel::Fold& v){a.Values(v.foldId,v.path,v.sense,v.angleRad);}
void Fields(Archive& a,fabrication::PatternPanel::Mate& v){a.Values(v.matePairId,v.path);}
void Fields(Archive& a,fabrication::PatternPanel& v){a.Values(v.panelId,v.outline,v.openings,v.reliefCuts,v.folds,v.mates);}
void Fields(Archive& a,app::PanelOrigin& v){a.Values(v.entityId,v.faceIndex);}
void Fields(Archive& a,fabrication::ApproximatedBand& v) {
    a.Values(v.number,v.minimumParameter,v.maximumParameter,v.widthMm,v.estimatedDeviationMm,v.planar);
}
void Fields(Archive& a,fabrication::BandApproximationResult& v) {
    a.Values(v.bands,v.maximumDeviationMm,v.reachedRequestedTolerance,v.railParameters,v.cornerParameters,v.narrowestWidthMm,v.narrowerThanMinimum);
}
void Fields(Archive& a,fabrication::BandMesh& v) {
    a.Values(v.rows,v.columns,v.world,v.developed,v.creaseDirections,v.longitudinalParameters,v.developedBands);
}
void Fields(Archive& a,app::FabricationEvaluation::BandedFace& v){a.Values(v.entityId,v.faceIndex,v.bands,v.mesh);}
void Fields(Archive& a,fabrication::GptApproxPanel& v) {
    a.Values(v.origin,v.u,v.v,v.normal,v.profile,v.lengths,v.pattern,v.maximumMm,v.squaredMm,v.sampleCount);
}
void Fields(Archive& a,app::FabricationEvaluation::MaterialPanel& v) {
    a.Values(v.panelIndex,v.bandIndex,v.axis,v.low,v.high,v.longitudinal,v.loopsUv);
}
void Fields(Archive& a,app::FabricationEvaluation& v) {
    std::vector<int> materialFlags;
    if (!a.read) for (bool flag : v.materialBands) materialFlags.push_back(flag ? 1 : 0);
    a.Values(v.method,v.panels,v.panelOrigins,v.bands,v.bandMesh,v.bandFaces,v.materialPanels,
        materialFlags,v.bandPanels,v.bandPanelCount,v.maximumDeviationMm,
        v.reachedTolerance,v.rmsDeviationMm,v.maximumSeamGapMm,v.gptPanels,v.summaryJa);
    if (a.read) {
        v.materialBands.clear();
        for (int flag : materialFlags) {
            if (flag != 0 && flag != 1) throw std::runtime_error("Invalid material band flag");
            v.materialBands.push_back(flag != 0);
        }
    }
}
void Fields(Archive& a,modeling::GuideTableRow& v) {
    a.Values(v.role,v.sourceWireIds,v.sourceLabels,v.segments,v.reversed,v.continuity,v.supportSurfaceId);
}
void Fields(Archive& a,modeling::GuideTable& v) {
    a.Values(v.method,v.rows,v.offsetDistanceMm,v.revolveAxisPoint,v.revolveAxisDirection,v.revolveAngleRad,v.lockSectionOrder,v.fourEdgeStyle);
}
void Fields(Archive& a,modeling::MeshTriangle& v) {
    for(auto& p:v.points)a.Values(p);
    a.Values(v.normal,v.faceIndex);
    for(auto& n:v.vertexNormals)a.Values(n);
}
void Fields(Archive& a,modeling::ShapeMesh& v) {
    a.Values(v.triangles,v.edges,v.isoLines,v.minimum,v.maximum,v.closed,v.faceCount);
}
void Fields(Archive& a,V2PreparedDocument& v) {
    a.Values(v.archive,v.parts,v.guides,v.partEdges,v.flatBoundaries,v.guideEdges,v.samples,v.fabrication,v.guideTable,v.meshes);
}
template<class... T> void Archive::Values(T&... values){(Field(values),...);}
template<class T> void Archive::Field(T& v) {
    if constexpr(std::is_same_v<T,bool>) {
        quint8 n=v?1:0;if(read)stream>>n;else stream<<n;if(n>1)throw std::runtime_error("Invalid boolean");v=n!=0;
    } else if constexpr(std::is_enum_v<T>) {
        int n=static_cast<int>(v);Field(n);if(n<0||n>255)throw std::runtime_error("Invalid enum");v=static_cast<T>(n);
    } else if constexpr(std::is_integral_v<T>) {
        using Wire=std::conditional_t<std::is_signed_v<T>,qint64,quint64>;
        Wire n=v;if(read)stream>>n;else stream<<n;
        if(n<std::numeric_limits<T>::lowest()||n>std::numeric_limits<T>::max())throw std::runtime_error("Integer overflow");v=static_cast<T>(n);
    } else if constexpr(std::is_floating_point_v<T>) {
        double n=v;if(read)stream>>n;else stream<<n;if(!std::isfinite(n))throw std::runtime_error("Non-finite number");v=static_cast<T>(n);
    } else if constexpr(std::is_same_v<T,std::string>) {
        const auto n=Count(v.size(),512*1024*1024);
        if(read) {
            if(static_cast<qint64>(n)>stream.device()->bytesAvailable())throw std::runtime_error("Truncated string");
            v.resize(n);if(stream.readRawData(v.data(),static_cast<int>(n))!=static_cast<int>(n))throw std::runtime_error("Truncated string");
        } else if(stream.writeRawData(v.data(),static_cast<int>(n))!=static_cast<int>(n))throw std::runtime_error("Cannot write result");
    } else if constexpr(IsVector<T>::value) {
        const auto n=Count(v.size());if(n>remainingElements)throw std::runtime_error("Too many result elements");remainingElements-=n;
        if(read) {v.clear();v.reserve(n);for(std::size_t i=0;i<n;++i){auto x=EmptyValue<typename T::value_type>();Field(x);v.push_back(std::move(x));}}
        else for(auto& x:v)Field(x);
    } else if constexpr(IsOptional<T>::value) {
        bool present=v.has_value();Field(present);if(read){if(present)v.emplace();else v.reset();}if(present)Field(*v);
    } else if constexpr(IsMap<T>::value) {
        const auto n=Count(v.size());if(n>remainingElements)throw std::runtime_error("Too many result entries");remainingElements-=n;
        if(read) {v.clear();for(std::size_t i=0;i<n;++i){typename T::key_type key;typename T::mapped_type value;Values(key,value);
            if(!v.emplace(std::move(key),std::move(value)).second)throw std::runtime_error("Duplicate result entry");}}
        else for(auto& entry:v){auto key=entry.first;Values(key,entry.second);}
    } else Fields(*this,v);
    Check();
}
}

bool CopyPreparedDocument(const QString& source,const QString& sourceToken,
    const QString& destination,const QString& destinationToken,const std::function<bool()>& pulse) {
    try {
        if(source==destination||sourceToken.isEmpty()||destinationToken.isEmpty())return false;
        QFile input(source);
        if(!input.open(QIODevice::ReadOnly)||input.size()>1024LL*1024*1024)return false;
        Archive reader(&input,true,pulse);std::string magic,id;int version=0;
        reader.Values(magic,version,id);
        if(magic!="kachakacha-prepared-document"||version!=3||id!=sourceToken.toStdString()||input.atEnd())return false;
        QSaveFile output(destination);if(!output.open(QIODevice::WriteOnly))return false;
        Archive writer(&output,false);id=destinationToken.toStdString();writer.Values(magic,version,id);
        while(!input.atEnd()) {
            if(!pulse())return false;
            const auto bytes=input.read(1024*1024);
            if(bytes.isEmpty()||output.write(bytes)!=bytes.size())return false;
        }
        return input.error()==QFile::NoError&&output.commit();
    }catch(...){return false;}
}

bool WritePreparedDocument(const QString& path,const QString& token,V2PreparedDocument& data) {
    try {
        QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
        Archive stream(&file,false);std::string magic="kachakacha-prepared-document",id=token.toStdString();int version=3;
        stream.Values(magic,version,id,data);stream.Check();return file.commit();
    }catch(...){return false;}
}
kachakacha::v2::base::Result<std::unique_ptr<V2PreparedDocument>> ReadPreparedDocument(const QString& path,const QString& token,const std::function<bool()>& pulse) {
    using namespace kachakacha::v2;
    using Result=base::Result<std::unique_ptr<V2PreparedDocument>>;
    try {
        QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>1024LL*1024*1024)throw std::runtime_error("Cannot read calculation result");
        Archive stream(&file,true,pulse);std::string magic,id;int version=0;stream.Values(magic,version,id);
        if(magic!="kachakacha-prepared-document"||version!=3||id!=token.toStdString())throw std::runtime_error("Calculation result identity mismatch");
        auto data=std::make_unique<V2PreparedDocument>();stream.Values(*data);stream.Check();if(!file.atEnd())throw std::runtime_error("Unexpected result tail");
        return Result::Success(std::move(data));
    }catch(const std::exception& e) {
        return Result::Failure(base::MakeError(app::kGeometryJobFailed,"文書の再構築結果を読み込めませんでした。元の文書は保持しています。",e.what()));
    }
}
