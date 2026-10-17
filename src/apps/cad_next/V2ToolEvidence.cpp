#include <QIODevice>
#include "V2ToolEvidence.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QString>
#include <map>
#include <algorithm>
#include <set>
namespace V2ToolEvidence {
namespace {
QString currentCase;std::set<QString> seen;std::map<QString,int> scores;QJsonArray calls;
QString Directory(){return qEnvironmentVariable("KACHACAD_TOOL_AUDIT_DIR");}
}
void BeginCase(const char* name){if(Directory().isEmpty())return;currentCase=QString::fromUtf8(name);seen.clear();calls={};}
Command::Command(V2MainWindow& window,std::string_view id):window_(window),id_(id){
    if(Directory().isEmpty())return;
    const auto key=QString::fromUtf8(id.data(),int(id.size()));
    calls.append(QJsonObject{{"command",key},{"selected",int(window.Viewport().Selection().entityIds.size())},
        {"entities",int(window.Session().GetDocument().Snapshot().entities.size())}});
    // Prefer a populated workflow to the empty-input reachability sweep.
    const int score=int(window.Viewport().Selection().entityIds.size())*10
        +std::min(20,int(window.Session().GetDocument().Snapshot().entities.size()));
    capture_=!seen.contains(key)&&(!scores.contains(key)||score>scores[key]);seen.insert(key);
    if(capture_){scores[key]=score;QDir dir(Directory());dir.mkpath("commands");window.grab().save(dir.filePath("commands/"+key+"-before.png"));}
}
Command::~Command(){
    if(!capture_)return;
    QDir dir(Directory());const auto key=QString::fromUtf8(id_.data(),int(id_.size()));
    window_.grab().save(dir.filePath("commands/"+key+"-after.png"));
    QFile file(dir.filePath("commands/"+key+".json"));if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{
        {"command",key},{"case",currentCase},{"phase","command returned; not proof of committed geometry"},
        {"status",window_.StatusText()},{"at",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}).toJson());
}
void EndCase(V2MainWindow& window,bool passed){
    if(Directory().isEmpty())return;
    QDir dir(Directory());dir.mkpath(".");QFile file(dir.filePath("cases.jsonl"));
    if(file.open(QIODevice::Append))file.write(QJsonDocument(QJsonObject{{"case",currentCase},{"passed",passed},{"calls",calls},
        {"at",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}).toJson(QJsonDocument::Compact)+'\n');
    (void)window;
}
}
