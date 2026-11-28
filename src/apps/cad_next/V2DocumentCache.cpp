#include "V2DocumentCache.h"
#include "V2DocumentTiming.h"
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
#include <QThread>
#include <QtGlobal>

#include <array>
#include <atomic>
#include <filesystem>
#include <future>
#include <system_error>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <bcrypt.h>

#include <limits>
#endif

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
#ifdef Q_OS_WIN
    // The OS SHA-256 provider can use the CPU's accelerated implementation.
    // Mapping avoids copying a whole prepared document into another buffer.
    const auto size = file.size();
    if (size > 0 && size <= (std::numeric_limits<ULONG>::max)()) {
        if (auto* data = file.map(0, size)) {
            QByteArray digest(QCryptographicHash::hashLength(QCryptographicHash::Sha256), '\0');
            const auto status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, data,
                static_cast<ULONG>(size), reinterpret_cast<PUCHAR>(digest.data()),
                static_cast<ULONG>(digest.size()));
            file.unmap(data);
            if (status >= 0) return digest.toHex();
        }
    }
#endif
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
bool WriteManifest(const QDir& dir,const QString& key,const QString& path,const QString& payloadToken) {
    const auto size=QFileInfo(path).size();
    if(size<=0||size>entryLimit){QFile::remove(path);return false;}
    const auto digest=DigestFile(path);if(digest.isEmpty())return false;
    QJsonObject info{{"key",key},{"size",double(size)},{"sha256",QString::fromLatin1(digest)}};
    if(payloadToken!=key)info.insert("payloadToken",payloadToken);
    const auto bytes=QJsonDocument(info).toJson(QJsonDocument::Compact);
    QSaveFile manifest(dir.filePath(key+".json"));
    const bool ok=manifest.open(QIODevice::WriteOnly)&&manifest.write(bytes)==bytes.size()&&manifest.commit();
    Prune(dir);return ok;
}

bool ValidateNativeShapes(const V2PreparedDocument& data)
{
    std::vector<const std::string*> shapes;
    shapes.reserve(data.parts.size() + data.guides.size());
    for (const auto* entries : {&data.parts, &data.guides}) {
        for (const auto& entry : *entries) shapes.push_back(&entry.second);
    }
    // Each payload restores a fresh shape. The kernel protects its handle table.
    // Bound concurrent native shapes and retain the normal serial test route.
    const bool parallel = shapes.size() >= 64 && QThread::idealThreadCount() >= 4
        && !qEnvironmentVariableIsSet("KACHACAD_OPEN_SERIAL");
    const std::size_t lanes = parallel ? 3 : 1;
    std::atomic_bool valid{true};
    const auto validateLane = [&](std::size_t lane) {
        for (std::size_t index = lane; index < shapes.size() && valid.load(std::memory_order_relaxed);
            index += lanes) {
            const auto shape = kachakacha::v2::kernel::RestoreTransferShape(*shapes[index]);
            if (!shape.HasValue()) {
                valid.store(false, std::memory_order_relaxed);
                return;
            }
            kachakacha::v2::kernel::ReleaseShape(shape.Value());
        }
    };
    std::array<std::future<void>, 2> workers;
    for (std::size_t lane = 1; lane < lanes; ++lane) {
        try {
            workers[lane - 1] = std::async(std::launch::async, validateLane, lane);
        } catch (const std::system_error&) {
            validateLane(lane);
        }
    }
    validateLane(0);
    for (auto& worker : workers) {
        if (worker.valid()) worker.get();
    }
    return valid.load(std::memory_order_relaxed);
}

struct CachePayload {
    QString path;
    QString token;
};

std::optional<CachePayload> MatchingPayload(const QString& key)
{
    if(!OwnedName(key+".bin"))return std::nullopt;
    const QDir dir(Root());
    const auto path=dir.filePath(key+".bin");
    QFile manifest(dir.filePath(key+".json"));
    if(!manifest.open(QIODevice::ReadOnly)||manifest.size()>4096)return std::nullopt;
    const auto info=QJsonDocument::fromJson(manifest.readAll()).object();
    const auto size=QFileInfo(path).size();
    const auto storedToken=info.value("payloadToken");
    const auto token=storedToken.isUndefined()?key:storedToken.toString();
    if(token.isEmpty()||token.size()>256||info.value("key").toString()!=key||size<=0||size>entryLimit
        ||info.value("size").toDouble()!=size||info.value("sha256").toString().toLatin1()!=DigestFile(path))
        return std::nullopt;
    return CachePayload{path,token};
}

std::optional<V2PreparedDocument> ReadPayload(const CachePayload& payload)
{
    auto restored=ReadPreparedDocument(payload.path,payload.token,[]{return true;});
    if(!restored.HasValue())return std::nullopt;
    return std::move(*restored.Value());
}

bool LinkCompletedTransfer(const QString& source, const QString& destination, const QString& token)
{
    if(qEnvironmentVariableIsSet("KACHACAD_FORCE_COPY_DOCUMENT_CACHE"))return false;
    const QFileInfo original(source), target(destination);
    const auto size=original.size();
    if(size<=0||size>entryLimit||original.isSymLink()||!original.isFile()||target.exists()
        ||token.size()>256||!PreparedDocumentHasToken(source,token))return false;
    std::error_code error;
    // The job closes its output before publishing the link. The digest
    // and full shape validation still gate any later use of this cache.
    std::filesystem::create_hard_link(original.filesystemAbsoluteFilePath(),
        target.filesystemAbsoluteFilePath(),error);
    return !error;
}
}
QString Key(const std::string& source) {
    const auto binary=BinaryIdentity();if(binary.isEmpty())return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData("prepared-cache-v1");hash.addData(binary);
    hash.addData(QByteArray::fromStdString(source));return QString::fromLatin1(hash.result().toHex());
}
bool HasMatchingPayload(const std::string& source) {
    const auto payload=MatchingPayload(Key(source));
    return payload&&PreparedDocumentHasToken(payload->path,payload->token);
}
std::optional<V2PreparedDocument> ReadKey(const QString& key) {
    const auto payload=MatchingPayload(key);
    return payload?ReadPayload(*payload):std::nullopt;
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
    return WriteManifest(dir,key,path,key);
}
bool Transfer(const std::string& source,const QString& destination,const QString& token,TransferValidation validation) {
    V2DocumentTiming timing("cache-restore");
    const auto payload=MatchingPayload(Key(source));if(!payload)return false;
    timing.Mark("verify-cache");
    if (validation == TransferValidation::InWorker) {
        const auto data=ReadPayload(*payload);if(!data)return false;
        timing.Mark("read-cache");
        const bool valid = ValidateNativeShapes(*data);
        timing.Mark("validate-native");
        if (!valid) return false;
    }
    // The document opener checks the complete frame and restores every native
    // shape before adoption. A rejected reply from this route is rebuilt.
    return CopyPreparedDocument(payload->path,payload->token,destination,token,
        [] { return true; }, V2PreparedFile::WorkerReply);
}
bool StoreTransfer(const std::string& source,const QString& transfer,const QString& token) {
    const QDir dir(Root());if(!QDir().mkpath(dir.absolutePath()))return false;
    const auto key=Key(source);if(key.isEmpty())return false;
    const auto path=dir.filePath(key+".bin");
    V2DocumentTiming timing("cache-store");
    const bool linked=LinkCompletedTransfer(transfer,path,token);
    if(!linked&&!CopyPreparedDocument(transfer,token,path,key))return false;
    timing.Mark(linked?"link-payload":"copy-payload");
    const bool saved=WriteManifest(dir,key,path,linked?token:key);
    timing.Mark("publish-manifest");
    return saved;
}
}
