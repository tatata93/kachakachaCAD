#include "V2GeometryJob.h"
#include "V2PreparedDocument.h"
#include "kachakacha/app/GeometryJobDiagnostic.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <QApplication>
#include <QByteArray>
#include <QChar>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <iostream>
#include <cmath>
#include <deque>

namespace V2GeometryJob {
using namespace kachakacha::v2;
namespace {
constexpr auto busyProperty="geometryJobActive";
constexpr auto failedCode=app::kGeometryJobFailed;
constexpr auto cancelledCode=app::kGeometryJobCancelled;
using JsonResult=base::Result<QJsonObject>;
struct PreviewCacheEntry {QByteArray key;QJsonObject result;qsizetype bytes=0;std::shared_ptr<V2PreparedDocument> prepared;};
std::deque<PreviewCacheEntry>& PreviewCache(){static std::deque<PreviewCacheEntry> entries;return entries;}
QByteArray PreviewKey(const QJsonObject& request,modeling::KernelShapeHandle source) {
    const auto captured=kernel::CaptureOutputShape(source);if(!captured.HasValue())return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QJsonDocument(request).toJson(QJsonDocument::Compact));
    hash.addData(QByteArray::fromStdString(captured.Value()));return hash.result();
}
void CachePreview(const QByteArray& key,const QJsonObject& result,const std::shared_ptr<V2PreparedDocument>& prepared,qsizetype preparedSize) {
    // Retain only validated geometry bytes, never live document IDs or handles.
    // Include native bytes, curves and meshes. No IDs or live handles are kept.
    // Four entries / 64 MiB also cover a detailed projected window on redo.
    constexpr qsizetype limit=64*1024*1024;
    const qsizetype bytes=2*QJsonDocument(result).toJson(QJsonDocument::Compact).size()+2*preparedSize;
    if(key.isEmpty()||bytes>limit)return;
    auto& entries=PreviewCache();qsizetype total=bytes;
    for(const auto& entry:entries)total+=entry.bytes;
    while(!entries.empty()&&(entries.size()>=4||total>limit)){total-=entries.front().bytes;entries.pop_front();}
    entries.push_back({key,result,bytes,prepared});
}
base::Diagnostic Failed(const QString& detail) {
    return base::MakeError(failedCode,"計算を完了できませんでした。文書は変更していません。",detail.toStdString());
}
class JobDialog final:public QDialog {
public:
    using QDialog::QDialog;
    std::function<void()> cancel;
    void reject() override { if(cancel)cancel(); }
};
bool WriteJson(const QString& path,const QJsonObject& object) {
    QSaveFile file(path);const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
}
QJsonObject ReadJson(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>256*1024*1024)return {};
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
    return error.error==QJsonParseError::NoError&&doc.isObject()?doc.object():QJsonObject{};
}
QJsonObject Diagnostic(const base::Diagnostic& d) {
    return {{"ok",false},{"code",QString::fromStdString(d.code)},
        {"summary",QString::fromStdString(d.summaryJa)},{"details",QString::fromStdString(d.detailsJa)}};
}
QJsonArray Point(const geometry::Vector3& p) {return {p.x,p.y,p.z};}
geometry::Vector3 Point(const QJsonValue& value) {
    const auto a=value.toArray();return {a.at(0).toDouble(),a.at(1).toDouble(),a.at(2).toDouble()};
}
void Phase(const char* text) {std::cout<<"KACHACAD_PHASE:"<<text<<std::endl;}

JsonResult ReadResponse(const QTemporaryDir& scratch,const QString& token,const DocumentReader& reader,
    const std::function<bool()>& pulse) {
    const auto response=ReadJson(scratch.filePath("response.json"));
    if(response.value("version").toInt()!=1||response.value("token").toString()!=token)
        return JsonResult::Failure(Failed(QStringLiteral("計算結果が不完全なため採用しませんでした。")));
    if(!response.value("ok").toBool())return JsonResult::Failure(base::MakeError(
        response.value("code").toString(failedCode).toStdString(),response.value("summary").toString(QStringLiteral("計算に失敗しました。")).toStdString(),response.value("details").toString().toStdString()));
    if(reader) {
        const auto loaded=reader(scratch.filePath("prepared.bin"),token,pulse);
        if(!loaded.HasValue())return JsonResult::Failure(loaded.Diagnostics());
    }
    return JsonResult::Success(response);
}

// The only process we may stop is this private calculation child. Its only
// writable outputs are scratch files; it never receives the document or a save path.
JsonResult Run(QWidget* parent,QJsonObject request,const std::vector<modeling::KernelShapeHandle>& inputs,
    const DocumentReader& reader={}, bool interactivePreview=false) {
    const bool documentJob=request.contains("source")&&!request.contains("operation");
    if(Active())return JsonResult::Failure(Failed(QStringLiteral("別の計算が終了してから再操作してください。")));
    struct Busy {
        Busy(){qApp->setProperty(busyProperty,true);}
        ~Busy(){qApp->setProperty(busyProperty,false);}
    } busy;
    QTemporaryDir scratch;
    if(!scratch.isValid())return JsonResult::Failure(Failed(QStringLiteral("計算用の一時フォルダーを作れません。")));
    QJsonArray shapes;
    for(const auto handle:inputs) {
        const auto captured=kernel::CaptureOutputShape(handle);
        if(!captured.HasValue())return JsonResult::Failure(captured.Diagnostics());
        shapes.append(QString::fromLatin1(QByteArray::fromStdString(captured.Value()).toBase64()));
    }
    const auto token=QUuid::createUuid().toString(QUuid::WithoutBraces);
    request["version"]=1;request["token"]=token;request["shapes"]=shapes;
    if(!WriteJson(scratch.filePath("request.json"),request))return JsonResult::Failure(Failed(QStringLiteral("計算用入力を書き込めません。")));
    JobDialog dialog(parent);dialog.setObjectName("geometryJobDialog");
    dialog.setWindowTitle(documentJob?QStringLiteral("文書を開く"):QStringLiteral("形状を計算"));
    dialog.setWindowModality(interactivePreview?Qt::NonModal:Qt::ApplicationModal);
    dialog.setProperty("interactiveGeometryPreview",interactivePreview);
    if(interactivePreview)dialog.setAttribute(Qt::WA_ShowWithoutActivating,true);
    dialog.setMinimumWidth(420);
    auto* layout=new QVBoxLayout(&dialog);
    auto* label=new QLabel(QStringLiteral("計算を準備しています…"),&dialog);label->setWordWrap(true);layout->addWidget(label);
    auto* progress=new QProgressBar(&dialog);progress->setRange(0,0);layout->addWidget(progress);
    auto* elapsed=new QLabel(&dialog);layout->addWidget(elapsed);
    auto* cancel=new QPushButton(QStringLiteral("計算を取り消す"),&dialog);cancel->setObjectName("geometryJobCancel");layout->addWidget(cancel);
    QProcess process(&dialog);process.setObjectName("geometryJobProcess");
    bool cancelled=false;bool exited=false;int exitCode=-1;std::optional<JsonResult> result;
    QByteArray output,errorOutput;
    QElapsedTimer clock;clock.start();QTimer timer;timer.setInterval(100);
    QObject::connect(&timer,&QTimer::timeout,&dialog,[&]{elapsed->setText(QStringLiteral("経過 %1 秒 — 完了するまで文書は変更しません").arg(clock.elapsed()/1000));});
    dialog.cancel=[&]{
        if(cancelled)return;cancelled=true;cancel->setEnabled(false);
        label->setText(QStringLiteral("計算を取り消しています…"));
        if(process.state()!=QProcess::NotRunning)process.kill();
        else dialog.done(QDialog::Rejected);
    };
    QObject::connect(cancel,&QPushButton::clicked,&dialog,[&]{dialog.reject();});
    QObject::connect(&process,&QProcess::started,&dialog,[&]{dialog.setProperty("calculationPid",process.processId());});
    QObject::connect(&process,&QProcess::readyReadStandardOutput,&dialog,[&]{
        output+=process.readAllStandardOutput();
        while(output.contains('\n')) {
            const auto line=output.left(output.indexOf('\n')).trimmed();output.remove(0,output.indexOf('\n')+1);
            if(!cancelled&&line.startsWith("KACHACAD_PHASE:")) {
                const auto text=QString::fromUtf8(line.mid(15));label->setText(text);dialog.setProperty("calculationPhase",text);
            }
        }
        if(output.size()>4096)output=output.right(4096);
    });
    QObject::connect(&process,&QProcess::readyReadStandardError,&dialog,[&]{errorOutput=(errorOutput+process.readAllStandardError()).right(4096);});
    QObject::connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&dialog,[&](int code,QProcess::ExitStatus status){
        exited=true;exitCode=status==QProcess::NormalExit?code:-1;
        QTimer::singleShot(0,&dialog,[&]{
            if(!cancelled&&exitCode==0) {
                label->setText(QStringLiteral("計算結果を読み込んでいます…"));
                result=ReadResponse(scratch,token,reader,[&]{QCoreApplication::processEvents();return !cancelled;});
            }
            dialog.done(QDialog::Accepted);
        });
    });
    QObject::connect(&process,&QProcess::errorOccurred,&dialog,[&](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart){exited=true;dialog.done(QDialog::Rejected);}
    });
    process.setWorkingDirectory(scratch.path());
    if(reader){auto environment=QProcessEnvironment::systemEnvironment();environment.insert("QT_QPA_PLATFORM","offscreen");process.setProcessEnvironment(environment);}
    QTimer::singleShot(0,&dialog,[&]{if(!cancelled)process.start(QCoreApplication::applicationFilePath(),{documentJob?"--document-job":"--geometry-job",scratch.path()});});
    timer.start();
    if(interactivePreview) {
        // Keep the viewport and parameter pane usable while the private child
        // validates the preview. Input changes cancel it and schedule the latest
        // request; document commands remain guarded by Active().
        QEventLoop loop;
        bool finished=false;
        QObject::connect(&dialog,&QDialog::finished,&loop,[&]{finished=true;loop.quit();});
        dialog.show();if(!finished)loop.exec();
    } else dialog.exec();
    // Also cover application shutdown while the modal event loop is running.
    if(process.state()!=QProcess::NotRunning){cancelled=true;process.kill();process.waitForFinished(3000);}
    if(cancelled)return JsonResult::Failure(base::MakeError(cancelledCode,"計算を取り消しました。文書は変更していません。","入力を変更するか、選び直して再計算できます。"));
    if(!exited||exitCode!=0)return JsonResult::Failure(Failed(QStringLiteral("計算用プロセスが正常に終了しませんでした。再試行できます。\n終了コード: %1\n%2")
        .arg(exitCode).arg(QString::fromUtf8(errorOutput))));
    return result?std::move(*result):JsonResult::Failure(Failed(QStringLiteral("計算結果を受け取れませんでした。")));
}
}
bool Active(){return QCoreApplication::instance()&&QCoreApplication::instance()->property(busyProperty).toBool();}
void CancelPreview(QWidget* parent) {
    if(!parent)return;
    auto* dialog=parent->findChild<QDialog*>("geometryJobDialog");
    if(dialog&&dialog->property("interactiveGeometryPreview").toBool())dialog->reject();
}

base::Result<bool> PrepareDocument(QWidget* parent,const QString& path,const DocumentReader& reader) {
    const auto result=Run(parent,{{"source",path}}, {}, reader);
    if(!result.HasValue())return base::Result<bool>::Failure(result.Diagnostics());
    return base::Result<bool>::Success(true);
}

int Worker(const QString& directory) {
    const QDir dir(directory);const auto request=ReadJson(dir.filePath("request.json"));
    if(request.value("version").toInt()!=1||request.value("token").toString().isEmpty())return 2;
    QJsonObject response;
    try {
        Phase("入力形状を読み込んでいます…");
        std::vector<modeling::KernelShapeHandle> shapes;
        for(const auto value:request.value("shapes").toArray()) {
            const auto shape=kernel::RestoreOutputShape(QByteArray::fromBase64(value.toString().toLatin1()).toStdString());
            if(!shape.HasValue()){response=Diagnostic(shape.FirstDiagnostic());break;}
            shapes.push_back(shape.Value());
        }
        modeling::KernelShapeHandle result;double volume=0,thickness=0,previous=0;
        kernel::ThickenedSolid wall;
        if(response.isEmpty()&&!shapes.empty()) {
            const auto operation=request.value("operation").toString();
            if(operation=="thicken") {
                Phase("厚みを計算し、閉鎖性・体積・自己交差を検査しています…");
                geometry::GeometryTolerance tolerance;
                tolerance.modelLinearMm=request.value("linear").toDouble();
                tolerance.modelAngularRad=request.value("angular").toDouble();
                tolerance.numericEpsilon=request.value("epsilon").toDouble();
                const auto built=request.value("toPlane").toBool()
                    ? kernel::ThickenSurfaceToPlane(shapes.front(),Point(request.value("origin")),Point(request.value("normal")),tolerance)
                    : kernel::ThickenSurface(shapes.front(),request.value("thickness").toDouble(),static_cast<fabrication::ThicknessPlacement>(request.value("placement").toInt()),tolerance,request.value("correctOpposite").toBool());
                if(!built.HasValue())response=Diagnostic(built.FirstDiagnostic());
                else {result=built.Value().handle;volume=built.Value().volumeMm3;thickness=built.Value().thicknessMm;wall=built.Value();}
            } else if(operation=="boolean"&&shapes.size()>1) {
                Phase("部品の結合・切取りと安全検査を行っています…");
                const auto built=kernel::BuildBooleans(static_cast<kernel::BooleanOperation>(request.value("kind").toInt()),shapes.front(),{shapes.begin()+1,shapes.end()},request.value("linear").toDouble());
                if(!built.HasValue())response=Diagnostic(built.FirstDiagnostic());
                else {result=built.Value().handle;volume=built.Value().volumeMm3;previous=built.Value().previousVolumeMm3;}
            }
        }
        if(result.Valid()) {
            Phase("計算結果を転送しています…");
            const auto captured=kernel::CaptureOutputShape(result);
            if(!captured.HasValue())response=Diagnostic(captured.FirstDiagnostic());
            else response={{"ok",true},{"brep",QString::fromLatin1(QByteArray::fromStdString(captured.Value()).toBase64())},
                {"volume",volume},{"thickness",thickness},{"previous",previous},
                {"corrected",wall.corrected},{"normalMin",wall.normalThicknessMinMm},{"normalMax",wall.normalThicknessMaxMm},
                {"maxDeviation",wall.maxOffsetDeviationMm},{"rmsDeviation",wall.rmsOffsetDeviationMm}};
            if(response.value("ok").toBool()&&request.value("operation").toString()=="thicken"){
                Phase("境界ワイヤーを準備しています…");
                V2PreparedDocument prepared;prepared.parts["result"]=captured.Value();
                const auto wires=kernel::OutputBoundaryWires(result,request.value("linear").toDouble());
                QJsonArray notes;
                const auto diagnostics=wires.HasValue()?wires.Diagnostics():std::vector<base::Diagnostic>{
                    base::MakeWarning("EXP-W002","境界ワイヤーを取り出せませんでした。",wires.FirstSummaryJa())};
                for(const auto& note:diagnostics)notes.append(QJsonObject{{"code",QString::fromStdString(note.code)},
                    {"severity",static_cast<int>(note.severity)},{"summary",QString::fromStdString(note.summaryJa)},
                    {"details",QString::fromStdString(note.detailsJa)}});
                response["boundaryDiagnostics"]=notes;
                response["boundaryCount"]=wires.HasValue()?static_cast<int>(wires.Value().size()):0;
                if(wires.HasValue())for(std::size_t i=0;i<wires.Value().size();++i)
                    prepared.partEdges["wire-"+std::to_string(i)]=wires.Value()[i];
                if(request.value("displayMesh").toBool()){
                    Phase("表示用の形を準備しています…");
                    const auto mesh=kernel::BuildShapeMesh(result);
                    if(!mesh.HasValue())response=Diagnostic(mesh.FirstDiagnostic());
                    else prepared.meshes["result"]=mesh.Value();
                }
                if(response.value("ok").toBool()){
                    const auto path=dir.filePath("prepared.bin");
                    if(!WritePreparedDocument(path,request.value("token").toString(),prepared))
                        response=Diagnostic(Failed(QStringLiteral("厚みの表示結果を書き込めませんでした。")));
                    else response["preparedBytes"]=QFileInfo(path).size();
                }
            }
        }
        if(response.isEmpty())response=Diagnostic(Failed(QStringLiteral("計算の入力が不正です。")));
    }catch(const std::exception& e){response=Diagnostic(Failed(QString::fromUtf8(e.what())));}
    catch(...){response=Diagnostic(Failed(QStringLiteral("幾何計算で例外が発生しました。")));}
    response["version"]=1;response["token"]=request.value("token");
    return WriteJson(dir.filePath("response.json"),response)?0:3;
}

base::Result<kernel::ThickenedSolid> Thicken(QWidget* parent,modeling::KernelShapeHandle source,
    double thickness,fabrication::ThicknessPlacement placement,const std::optional<modeling::WorkPlaneFrame>& plane,
    const geometry::GeometryTolerance& tolerance,bool interactivePreview,bool correctOpposite,modeling::ShapeMesh* displayMesh,bool cachedOnly,
    std::vector<std::vector<geometry::CurveSegment>>* boundaryWires) {
    using Out=base::Result<kernel::ThickenedSolid>;
    if(Active())return Out::Failure(Failed(QStringLiteral("別の計算が終了してから再操作してください。")));
    QJsonObject request{{"operation","thicken"},{"correctOpposite",correctOpposite},{"thickness",thickness},{"placement",static_cast<int>(placement)},
        {"linear",tolerance.modelLinearMm},{"angular",tolerance.modelAngularRad},{"epsilon",tolerance.numericEpsilon},{"toPlane",plane.has_value()},
        {"displayMesh",displayMesh!=nullptr}};
    if(plane){request["origin"]=Point(plane->origin);request["normal"]=Point(plane->normal);}
    const auto key=(interactivePreview||cachedOnly)?PreviewKey(request,source):QByteArray{};
    std::optional<QJsonObject> cached;
    std::shared_ptr<V2PreparedDocument> prepared;
    qsizetype preparedSize=0;
    if(!key.isEmpty())for(const auto& entry:PreviewCache())if(entry.key==key){cached=entry.result;prepared=entry.prepared;break;}
    if(cachedOnly&&!cached)return Out::Failure(Failed(QStringLiteral("同じ形状・設定の厚み計算結果はありません。")));
    const DocumentReader read=[&](const auto& path,const auto& token,const auto& pulse){
        auto loaded=ReadPreparedDocument(path,token,pulse);
        if(!loaded.HasValue())return base::Result<bool>::Failure(loaded.Diagnostics());
        preparedSize=static_cast<qsizetype>(QFileInfo(path).size());
        prepared=std::make_shared<V2PreparedDocument>(std::move(*loaded.Value()));return base::Result<bool>::Success(true);
    };
    const auto response=cached?JsonResult::Success(*cached):Run(parent,request,{source},read,interactivePreview);
    if(!response.HasValue())return Out::Failure(response.Diagnostics());
    const auto& data=response.Value();
    const auto brep=QByteArray::fromBase64(data.value("brep").toString().toLatin1()).toStdString();
    const int boundaryCount=data.value("boundaryCount").toInt(-1);
    if(!prepared||prepared->parts.size()!=1||!prepared->parts.contains("result")||prepared->parts.at("result")!=brep
        ||boundaryCount<0||boundaryCount>1000000||prepared->partEdges.size()!=static_cast<std::size_t>(boundaryCount)
        ||!prepared->archive.empty()||!prepared->guides.empty()||!prepared->flatBoundaries.empty()||!prepared->guideEdges.empty()
        ||!prepared->samples.empty()||!prepared->fabrication.empty()||!prepared->guideTable.rows.empty()
        ||prepared->meshes.size()!=(displayMesh?1u:0u)||(displayMesh&&(!prepared->meshes.contains("result")||prepared->meshes.at("result").Empty())))
        return Out::Failure(Failed(QStringLiteral("厚みの形状・境界・表示の結果が一致しません。")));
    for(int i=0;i<boundaryCount;++i)if(!prepared->partEdges.contains("wire-"+std::to_string(i))
        ||prepared->partEdges.at("wire-"+std::to_string(i)).empty())return Out::Failure(Failed(QStringLiteral("厚みの境界結果が不完全です。")));
    std::vector<base::Diagnostic> notes;
    if(!data.value("boundaryDiagnostics").isArray())return Out::Failure(Failed(QStringLiteral("厚みの境界診断が不完全です。")));
    for(const auto value:data.value("boundaryDiagnostics").toArray()){
        const auto note=value.toObject();const int severity=note.value("severity").toInt(-1);
        if(severity<0||severity>1||note.value("code").toString().isEmpty()||note.value("summary").toString().isEmpty())
            return Out::Failure(Failed(QStringLiteral("厚みの境界診断が不正です。")));
        base::Diagnostic diagnostic;diagnostic.code=note.value("code").toString().toStdString();diagnostic.severity=static_cast<base::Severity>(severity);
        diagnostic.summaryJa=note.value("summary").toString().toStdString();diagnostic.detailsJa=note.value("details").toString().toStdString();notes.push_back(std::move(diagnostic));
    }
    const auto handle=kernel::RestoreOutputShape(brep);
    if(!handle.HasValue())return Out::Failure(handle.Diagnostics());
    if(!cached)CachePreview(key,data,prepared,preparedSize);
    kernel::ThickenedSolid made;made.handle=handle.Value();made.volumeMm3=data.value("volume").toDouble();made.thicknessMm=data.value("thickness").toDouble();
    made.corrected=data.value("corrected").toBool();
    made.normalThicknessMinMm=data.value("normalMin").toDouble();made.normalThicknessMaxMm=data.value("normalMax").toDouble();
    made.maxOffsetDeviationMm=data.value("maxDeviation").toDouble();made.rmsOffsetDeviationMm=data.value("rmsDeviation").toDouble();
    if(boundaryWires)boundaryWires->clear();
    for(int i=0;i<boundaryCount;++i){
        const auto& wire=prepared->partEdges.at("wire-"+std::to_string(i));
        made.edges.insert(made.edges.end(),wire.begin(),wire.end());if(boundaryWires)boundaryWires->push_back(wire);
    }
    if(displayMesh)*displayMesh=prepared->meshes.at("result");
    return Out::Success(std::move(made),std::move(notes));
}
base::Result<kernel::BooleanBuildResult> Boolean(QWidget* parent,kernel::BooleanOperation operation,
    modeling::KernelShapeHandle first,const std::vector<modeling::KernelShapeHandle>& tools,double tolerance) {
    using Out=base::Result<kernel::BooleanBuildResult>;
    auto inputs=tools;inputs.insert(inputs.begin(),first);
    const auto response=Run(parent,{{"operation","boolean"},{"kind",static_cast<int>(operation)},{"linear",tolerance}},inputs);
    if(!response.HasValue())return Out::Failure(response.Diagnostics());
    const auto& data=response.Value();const auto handle=kernel::RestoreOutputShape(QByteArray::fromBase64(data.value("brep").toString().toLatin1()).toStdString());
    if(!handle.HasValue())return Out::Failure(handle.Diagnostics());
    return Out::Success({handle.Value(),data.value("volume").toDouble(),data.value("previous").toDouble()});
}
}
