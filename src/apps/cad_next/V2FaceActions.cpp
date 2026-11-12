#include "V2FaceActions.h"
#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"
#include "V2Viewport.h"
#include "V2ToolSelection.h"
#include "V2ShapeLifetime.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/document/Commands.h"
#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
using namespace kachakacha::v2;

bool V2FaceActions::Run(V2MainWindow& window,std::string_view command)
{
    if(command!="fabrication.create" && command!="workplane.from_face" && command!="fabrication.from_face")return false;
    const auto selection=window.viewport_->Selection();
    std::vector<app::SelectionRef> faces;
    for(const auto& ref:selection.ordered) {
        if(ref.kind!=app::SelectionElementKind::Face || !ref.pickedFaceIndex
            || !window.partShapes_.contains(ref.entityId.ToString()))continue;
        if(std::none_of(faces.begin(),faces.end(),[&](const auto& old){
            return old.entityId==ref.entityId && old.pickedFaceIndex==ref.pickedFaceIndex;}))faces.push_back(ref);
    }
    if(command=="fabrication.create") {
        if(faces.empty())return false;
        command="fabrication.from_face";
    }
    if(command!="workplane.from_face" && command!="fabrication.from_face")return false;
    window.EndArmedTools();window.ClearPendingCommand();
    window.SelectTool(modeling::DrawingTool::Select);
    auto* panel=new V2FaceActions(window,std::string(command));panel->sources_=selection;
    window.operationHost_->ShowTemporaryPage(panel,command=="workplane.from_face"
        ? QStringLiteral("フェイスで作図") : QStringLiteral("立体の面を近似"));
    if(!faces.empty())panel->Apply(faces);
    else panel->Pick();
    return true;
}
V2FaceActions::V2FaceActions(V2MainWindow& window,std::string command)
    :QWidget(&window),window_(window),command_(std::move(command))
{
    setObjectName(QStringLiteral("faceActionPanel"));auto* layout=new QVBoxLayout(this);
    status_=new QLabel(command_=="workplane.from_face"
        ? QStringLiteral("立体の面を選んでください。曲面では選んだ位置の接平面に作業面を作ります。")
        : QStringLiteral("近似する立体の面を選んでください。選んだ面を独立して取り出し、近似を始めます。"),this);
    status_->setObjectName(QStringLiteral("faceActionStatus"));status_->setWordWrap(true);layout->addWidget(status_);layout->addStretch();
    auto* cancel=new QPushButton(QStringLiteral("取消 Esc"),this);cancel->setObjectName(QStringLiteral("faceActionCancel"));layout->addWidget(cancel);
    QObject::connect(cancel,&QPushButton::clicked,this,[this]{window_.RunCommand("selection.activate");});
    qApp->installEventFilter(this);
}
void V2FaceActions::Pick()
{
    picking_=true;const QPointer<V2FaceActions> self(this);
    window_.viewport_->BeginElementPick([self](const app::PickCandidate& candidate){
        return self && self->isVisible() && candidate.kind==app::SelectionElementKind::Face
            &&candidate.pickedFaceIndex && self->window_.partShapes_.contains(candidate.entityId.ToString());
    },[self](std::optional<app::PickCandidate> candidate){
        if(!self)return;self->picking_=false;
        if(!candidate)return; // Tool switching owns the cancellation and its next page.
        app::SelectionRef ref;ref.entityId=candidate->entityId;ref.kind=candidate->kind;
        ref.pickedFaceIndex=candidate->pickedFaceIndex;ref.hitPoint=candidate->hitPoint;
        self->Apply({ref});
    });
}
void V2FaceActions::closeEvent(QCloseEvent* event)
{
    if(picking_)window_.viewport_->CancelReferencePick();
    QWidget::closeEvent(event);
}
bool V2FaceActions::eventFilter(QObject* object,QEvent* event)
{
    if(!isVisible() || V2ToolSelection::Active(*window_.viewport_))return false;
    if(event->type()!=QEvent::KeyPress || static_cast<QKeyEvent*>(event)->key()!=Qt::Key_Escape)return false;
    const auto* widget=qobject_cast<QWidget*>(object);if(!widget || widget->window()!=window())return false;
    window_.RunCommand("selection.activate");return true;
}
void V2FaceActions::Apply(const std::vector<app::SelectionRef>& refs)
{
    if(refs.empty())return;
    if(command_!="workplane.from_face") {
        auto& window=window_;
        if(!Extract(refs)){Pick();return;}
        window.RunCommand("fabrication.create");return;
    }
    if(refs.size()!=1){status_->setText(QStringLiteral("作業面は1つの面から作ります。基準にする面を選んでください。"));Pick();return;}
    const auto& ref=refs.front();
    const auto found=window_.partShapes_.find(ref.entityId.ToString());
    if(found==window_.partShapes_.end() || !ref.pickedFaceIndex){Pick();return;}
    const auto face=kernel::OutputFace(found->second,*ref.pickedFaceIndex);
    if(!face.HasValue()){status_->setText(QString::fromStdString(face.FirstSummaryJa()));Pick();return;}
    v2ui::ShapeListOwner owner({face.Value()});
    const auto frame=kernel::OutputSurfaceFrame(face.Value(),ref.hitPoint);
    if(!frame.HasValue()){status_->setText(QString::fromStdString(frame.FirstSummaryJa()));Pick();return;}
    app::WorkPlaneChoice choice;choice.method=modeling::WorkPlaneMethod::PointNormal;
    choice.origin=frame.Value().origin;choice.normal=frame.Value().normal;choice.uAxis=frame.Value().xDirection;
    auto& window=window_;const auto revision=window.session_->GetDocument().Revision();
    window.CreateWorkPlaneFromChoice(choice,true);
    if(window.session_->GetDocument().Revision()==revision){Pick();return;}
    window.operationHost_->SetShelves({});
    window.SetMode(app::UiMode::Drawing);window.AlignViewToActiveWorkPlane();window.RunCommand("draw.line");
    window.SetStatus(QStringLiteral("フェイス上に作業面を作りました。曲面の場合は選択位置の接平面です。直線の始点を指定してください。"));
}
