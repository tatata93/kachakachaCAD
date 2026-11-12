#include "V2DocumentCache.h"
#include "kachakacha/kernel/OcctShapeTransfer.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QtGlobal>

namespace V2DocumentCache {
namespace {
// Detailed native models can exceed 128 MiB while still fitting the bounded
// cache comfortably. Keep two maximum-size entries, as before; never relax
// payload hashing or semantic validation to accommodate a larger document.
constexpr qint64 entryLimit=256LL*1024*1024, totalLimit=512LL*1024*1024;
QString Root() {
    const auto cacheOverride=qEnvironmentVariable("KACHACAD_DOCUMENT_CACHE_DIR");
    return cacheOverride.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/document-shapes-v1" : cacheOverride;
}
QByteArray DigestFile(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}
QByteArray BinaryIdentity() {
    // Compiled evaluator and transfer schema change together with the executable.
    // Also invalidate after replacing any colocated OCCT runtime library.
    static const auto identity=[] {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        const auto exe=QCoreApplication::applicationFilePath();
        const auto digest=DigestFile(exe);if(digest.isEmpty())return QByteArray{};hash.addData(digest);
        const QDir dir(QFileInfo(exe).absolutePath());
        for(const auto& file:dir.entryInfoList({"TK*.dll","libTK*.so*"},QDir::Files,QDir::Name)) {
            hash.addData(file.fileName().toUtf8());hash.addData(DigestFile(file.absoluteFilePath()));
        }
        return hash.result();
    }();
    return identity;
}
bool OwnedName(const QString& name) {
    const auto stem=name.left(64);
    if(stem.size()!=64 || name!=stem+".bin")return false;
    for(const auto c:stem)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
void Prune(const QDir& dir) {
    const auto entries=dir.entryInfoList({"*.bin"},QDir::Files,QDir::Time);
    int count=0;qint64 bytes=0;
    for(const auto& file:entries) {
        if(!OwnedName(file.fileName()))continue;
        bytes+=file.size();
        if(++count<=8&&bytes<=totalLimit)continue;
        QFile::remove(file.absoluteFilePath());
        QFile::remove(dir.filePath(file.completeBaseName()+".json"));
        QFile::remove(dir.filePath(file.completeBaseName()+".features.json"));
    }
}
bool WriteManifest(const QDir& dir,const QString& key,const QString& path) {
    const auto size=QFileInfo(path).size();
    if(size<=0||size>entryLimit){QFile::remove(path);return false;}
    const auto digest=DigestFile(path);if(digest.isEmpty())return false;
    const QJsonObject info{{"key",key},{"size",double(size)},{"sha256",QString::fromLatin1(digest)}};
    const auto bytes=QJsonDocument(info).toJson(QJsonDocument::Compact);
    QSaveFile manifest(dir.filePath(key+".json"));
    const bool ok=manifest.open(QIODevice::WriteOnly)&&manifest.write(bytes)==bytes.size()&&manifest.commit();
    Prune(dir);return ok;
}
}
QString Key(const std::string& source) {
    const auto binary=BinaryIdentity();if(binary.isEmpty())return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData("prepared-cache-v1");hash.addData(binary);
    hash.addData(QByteArray::fromStdString(source));return QString::fromLatin1(hash.result().toHex());
}
static bool HasMatchingKey(const QString& key) {
    if(!OwnedName(key+".bin"))return false;
    const QDir dir(Root());
    const auto path=dir.filePath(key+".bin");
    QFile manifest(dir.filePath(key+".json"));
    if(!manifest.open(QIODevice::ReadOnly)||manifest.size()>4096)return false;
    const auto info=QJsonDocument::fromJson(manifest.readAll()).object();
    const auto size=QFileInfo(path).size();
    if(info.value("key").toString()!=key||size<=0||size>entryLimit||info.value("size").toDouble()!=size
        ||info.value("sha256").toString().toLatin1()!=DigestFile(path))return false;
    return true;
}
bool HasMatchingPayload(const std::string& source) { return HasMatchingKey(Key(source)); }
std::optional<V2PreparedDocument> ReadKey(const QString& key) {
    if(!HasMatchingKey(key))return std::nullopt;
    const auto path=QDir(Root()).filePath(key+".bin");
    auto restored=ReadPreparedDocument(path,key,[]{return true;});
    if(!restored.HasValue())return std::nullopt;
    return std::move(*restored.Value());
}
std::optional<V2PreparedDocument> Read(const std::string& source) { return ReadKey(Key(source)); }
std::vector<FeatureIndex> FeatureIndexes() {
    const QDir dir(Root());std::vector<FeatureIndex> indexes;
    for(const auto& name:dir.entryList({"*.bin"},QDir::Files,QDir::Time)) {
        if(!OwnedName(name))continue;
        const auto key=name.left(64);
        QFile file(dir.filePath(key+".features.json"));
        if(file.open(QIODevice::ReadOnly)&&file.size()<=2*1024*1024)
            indexes.push_back({key,file.readAll()});
        if(indexes.size()==8)break;
    }
    return indexes;
}
void WriteFeatureIndex(const std::string& source,const QByteArray& contents) {
    const auto key=Key(source);
    if(key.isEmpty()||contents.isEmpty()||contents.size()>2*1024*1024)return;
    QSaveFile file(QDir(Root()).filePath(key+".features.json"));
    if(file.open(QIODevice::WriteOnly)&&file.write(contents)==contents.size())(void)file.commit();
}
bool Write(const std::string& source,V2PreparedDocument& data) {
    const QDir dir(Root());if(!QDir().mkpath(dir.absolutePath()))return false;
    const auto key=Key(source);if(key.isEmpty())return false;
    const auto path=dir.filePath(key+".bin");
    if(!WritePreparedDocument(path,key,data))return false;
    return WriteManifest(dir,key,path);
}
bool Transfer(const std::string& source,const QString& destination,const QString& token) {
    // Retain full semantic validation, including malformed data with a matching manifest hash.
    const auto data=Read(source);if(!data)return false;
    for(const auto* shapes:{&data->parts,&data->guides})for(const auto& [id,bytes]:*shapes) {
        const auto shape=kachakacha::v2::kernel::RestoreTransferShape(bytes);
        if(!shape.HasValue())return false;
        kachakacha::v2::kernel::ReleaseShape(shape.Value());
    }
    const auto key=Key(source);
    return CopyPreparedDocument(QDir(Root()).filePath(key+".bin"),key,destination,token);
}
bool StoreTransfer(const std::string& source,const QString& transfer,const QString& token) {
    const QDir dir(Root());if(!QDir().mkpath(dir.absolutePath()))return false;
    const auto key=Key(source);if(key.isEmpty())return false;
    const auto path=dir.filePath(key+".bin");
    if(!CopyPreparedDocument(transfer,token,path,key))return false;
    return WriteManifest(dir,key,path);
}
}
