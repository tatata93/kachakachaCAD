#include "V2GeometryJob.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <functional>
#include <iostream>
#include <cmath>

namespace V2GeometryJob {
using namespace kachakacha::v2;
namespace {
constexpr auto busyProperty="geometryJobActive";
constexpr auto failedCode="JOB-001";
constexpr auto cancelledCode="JOB-002";
using JsonResult=base::Result<QJsonObject>;
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

// The only process we may stop is this private calculation child. Its only
// writable outputs are scratch files; it never receives the document or a save path.
JsonResult Run(QWidget* parent,QJsonObject request,const std::vector<modeling::KernelShapeHandle>& inputs) {
    if(Active())return JsonResult::Failure(Failed(QStringLiteral("別の計算が終了してから再操作してください。")));
    struct Busy {
        Busy(){qApp->setProperty(busyProperty,true);}
        ~Busy(){qApp->setProperty(busyProperty,false);}
    } busy;
    QTemporaryDir scratch(QDir::tempPath()+QStringLiteral("/kachakacha-geometry-XXXXXX"));
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
    dialog.setWindowTitle(QStringLiteral("形状を計算"));dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setMinimumWidth(420);
    auto* layout=new QVBoxLayout(&dialog);
    auto* label=new QLabel(QStringLiteral("計算を準備しています…"),&dialog);label->setWordWrap(true);layout->addWidget(label);
    auto* progress=new QProgressBar(&dialog);progress->setRange(0,0);layout->addWidget(progress);
    auto* elapsed=new QLabel(&dialog);layout->addWidget(elapsed);
    auto* cancel=new QPushButton(QStringLiteral("計算を取り消す"),&dialog);cancel->setObjectName("geometryJobCancel");layout->addWidget(cancel);
    QProcess process(&dialog);process.setObjectName("geometryJobProcess");
    bool cancelled=false;bool exited=false;int exitCode=-1;
    QByteArray output,errorOutput;
    QElapsedTimer clock;clock.start();QTimer timer;timer.setInterval(100);
    QObject::connect(&timer,&QTimer::timeout,&dialog,[&]{elapsed->setText(QStringLiteral("経過 %1 秒 — 完了するまで文書は変更しません").arg(clock.elapsed()/1000));});
    dialog.cancel=[&]{
        if(cancelled||exited)return;cancelled=true;cancel->setEnabled(false);
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
        exited=true;exitCode=status==QProcess::NormalExit?code:-1;dialog.done(QDialog::Accepted);
    });
    QObject::connect(&process,&QProcess::errorOccurred,&dialog,[&](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart){exited=true;dialog.done(QDialog::Rejected);}
    });
    process.setWorkingDirectory(scratch.path());
    QTimer::singleShot(0,&dialog,[&]{if(!cancelled)process.start(QCoreApplication::applicationFilePath(),{"--geometry-job",scratch.path()});});
    timer.start();dialog.exec();
    // Also cover application shutdown while the modal event loop is running.
    if(process.state()!=QProcess::NotRunning){cancelled=true;process.kill();process.waitForFinished(3000);}
    if(cancelled)return JsonResult::Failure(base::MakeError(cancelledCode,"計算を取り消しました。文書は変更していません。","入力を変更するか、選び直して再計算できます。"));
    if(!exited||exitCode!=0)return JsonResult::Failure(Failed(QStringLiteral("計算用プロセスが正常に終了しませんでした。再試行できます。\n終了コード: %1\n%2")
        .arg(exitCode).arg(QString::fromUtf8(errorOutput))));
    const auto response=ReadJson(scratch.filePath("response.json"));
    if(response.value("version").toInt()!=1||response.value("token").toString()!=token)
        return JsonResult::Failure(Failed(QStringLiteral("計算結果が不完全なため採用しませんでした。")));
    if(!response.value("ok").toBool())return JsonResult::Failure(base::MakeError(
        response.value("code").toString(failedCode).toStdString(),response.value("summary").toString(QStringLiteral("計算に失敗しました。")).toStdString(),response.value("details").toString().toStdString()));
    return JsonResult::Success(response);
}
}
bool Active(){return QCoreApplication::instance()&&QCoreApplication::instance()->property(busyProperty).toBool();}

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
                    : kernel::ThickenSurface(shapes.front(),request.value("thickness").toDouble(),static_cast<fabrication::ThicknessPlacement>(request.value("placement").toInt()),tolerance);
                if(!built.HasValue())response=Diagnostic(built.FirstDiagnostic());
                else {result=built.Value().handle;volume=built.Value().volumeMm3;thickness=built.Value().thicknessMm;}
            } else if(operation=="boolean"&&shapes.size()>1) {
                Phase("部品の結合・切取りと安全検査を行っています…");
                const auto built=kernel::BuildBooleans(static_cast<kernel::BooleanOperation>(request.value("kind").toInt()),shapes.front(),{shapes.begin()+1,shapes.end()},request.value("linear").toDouble());
                if(!built.HasValue())response=Diagnostic(built.FirstDiagnostic());
                else {result=built.Value().handle;volume=built.Value().volumeMm3;previous=built.Value().previousVolumeMm3;}
            }
        }
        if(result.Valid()) {
            Phase("表示用の形状を準備しています…");
            const auto mesh=kernel::BuildShapeMesh(result);(void)mesh;
            const auto captured=kernel::CaptureOutputShape(result);
            if(!captured.HasValue())response=Diagnostic(captured.FirstDiagnostic());
            else response={{"ok",true},{"brep",QString::fromLatin1(QByteArray::fromStdString(captured.Value()).toBase64())},
                {"volume",volume},{"thickness",thickness},{"previous",previous}};
        }
        if(response.isEmpty())response=Diagnostic(Failed(QStringLiteral("計算の入力が不正です。")));
    }catch(const std::exception& e){response=Diagnostic(Failed(QString::fromUtf8(e.what())));}
    catch(...){response=Diagnostic(Failed(QStringLiteral("幾何計算で例外が発生しました。")));}
    response["version"]=1;response["token"]=request.value("token");
    return WriteJson(dir.filePath("response.json"),response)?0:3;
}

base::Result<kernel::ThickenedSolid> Thicken(QWidget* parent,modeling::KernelShapeHandle source,
    double thickness,fabrication::ThicknessPlacement placement,const std::optional<modeling::WorkPlaneFrame>& plane,
    const geometry::GeometryTolerance& tolerance) {
    using Out=base::Result<kernel::ThickenedSolid>;
    QJsonObject request{{"operation","thicken"},{"thickness",thickness},{"placement",static_cast<int>(placement)},
        {"linear",tolerance.modelLinearMm},{"angular",tolerance.modelAngularRad},{"epsilon",tolerance.numericEpsilon},{"toPlane",plane.has_value()}};
    if(plane){request["origin"]=Point(plane->origin);request["normal"]=Point(plane->normal);}
    const auto response=Run(parent,request,{source});if(!response.HasValue())return Out::Failure(response.Diagnostics());
    const auto& data=response.Value();const auto handle=kernel::RestoreOutputShape(QByteArray::fromBase64(data.value("brep").toString().toLatin1()).toStdString());
    if(!handle.HasValue())return Out::Failure(handle.Diagnostics());
    kernel::ThickenedSolid made;made.handle=handle.Value();made.volumeMm3=data.value("volume").toDouble();made.thicknessMm=data.value("thickness").toDouble();
    const auto edges=kernel::OutputEdges(made.handle,tolerance.modelLinearMm);
    if(edges.HasValue())made.edges=edges.Value();
    return Out::Success(std::move(made));
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
