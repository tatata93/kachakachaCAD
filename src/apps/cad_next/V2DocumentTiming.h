#pragma once
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// Optional, process-separated evidence for cold rebuild, transfer and UI adoption.
// No document data or source path is written to the trace.
class V2DocumentTiming final {
public:
    explicit V2DocumentTiming(const char* scope):scope_(QString::fromLatin1(scope)) {clock_.start();}
    void Mark(const char* stage) {
        const auto root=qEnvironmentVariable("KACHACAD_OPEN_TRACE_DIR");
        if(root.isEmpty())return;
        const double now=clock_.nsecsElapsed()/1e6;
        const QJsonObject event{{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"pid",double(QCoreApplication::applicationPid())},{"scope",scope_},
            {"stage",QString::fromLatin1(stage)},{"stage_ms",now-previous_},{"elapsed_ms",now}};
        previous_=now;
        if(!QDir().mkpath(root))return;
        QFile file(QDir(root).filePath(QStringLiteral("load-%1.jsonl").arg(QCoreApplication::applicationPid())));
        if(file.open(QIODevice::WriteOnly|QIODevice::Append))file.write(QJsonDocument(event).toJson(QJsonDocument::Compact)+'\n');
    }
private:
    QString scope_;
    QElapsedTimer clock_;
    double previous_=0;
};
