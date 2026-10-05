#include "V2FaceActions.h"
#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"
#include "V2Viewport.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/modeling/MeshPick.h"
#include "kachakacha/modeling/ToolController.h"
#include "kachakacha/document/Commands.h"
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>
#include <QPointF>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <limits>
using namespace kachakacha::v2;

bool V2FaceActions::Run(V2MainWindow& window,std::string_view command)
{
    if(command=="fabrication.create"||command=="fabrication.gpt_create"){
        const auto& selected=window.viewport_->Selection().ordered;
        if(selected.size()!=1||selected.front().kind!=app::SelectionElementKind::Face||
            !selected.front().pickedFaceIndex||!window.partShapes_.contains(selected.front().entityId.ToString()))return false;
        command=command=="fabrication.create"?"fabrication.from_face":"fabrication.gpt_from_face";
    }
    if(command!="workplane.from_face" && command!="fabrication.from_face" && command!="fabrication.gpt_from_face")return false;
    const auto selection=window.viewport_->Selection();
    window.EndArmedTools();window.ClearPendingCommand();
    window.SelectTool(modeling::DrawingTool::Select);
    auto* panel=new V2FaceActions(window,std::string(command));
    window.operationHost_->ShowTemporaryPage(panel,command=="workplane.from_face" ? QStringLiteral("フェイスで作図") : QStringLiteral("フェイスを近似"));
    if(selection.ordered.size()==1 && selection.ordered.front().kind==app::SelectionElementKind::Face
        && selection.ordered.front().pickedFaceIndex)panel->Apply(selection.ordered.front());
    return true;
}
V2FaceActions::V2FaceActions(V2MainWindow& window,std::string command)
    :QWidget(&window),window_(window),command_(std::move(command))
{
    setObjectName(QStringLiteral("faceActionPanel"));auto* layout=new QVBoxLayout(this);
    status_=new QLabel(command_=="workplane.from_face"
        ? QStringLiteral("ソリッドのフェイスをクリックしてください。作業面を自動生成します。曲面ではクリック位置の接平面になります。")
        : QStringLiteral("近似するソリッドのフェイスをクリックしてください。独立した面を取り出し、その面で近似を始めます。元ソリッドは変更しません。"),this);
    status_->setWordWrap(true);layout->addWidget(status_);layout->addStretch();
    auto* cancel=new QPushButton(QStringLiteral("取消 Esc"),this);layout->addWidget(cancel);
    QObject::connect(cancel,&QPushButton::clicked,this,[this]{window_.operationHost_->SetShelves({});});
    qApp->installEventFilter(this);
}
bool V2FaceActions::eventFilter(QObject* object,QEvent* event)
{
    if(!isVisible())return false;
    if(event->type()==QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape) {
        auto* widget=qobject_cast<QWidget*>(object);if(!widget || widget->window()!=window())return false;
        window_.operationHost_->SetShelves({});return true;
    }
    if(object!=window_.viewport_ || event->type()!=QEvent::MouseButtonPress)return false;
    const auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton || mouse->modifiers()!=Qt::NoModifier)return false;
    if(window_.viewport_->PressViewNavigator(mouse->position(),view::AxisArrowModifier::None)!=V2Viewport::ViewPress::None)return true;
    const auto ray=window_.viewport_->Mapping().RayThrough({mouse->position().x(),mouse->position().y()});if(!ray)return true;
    double nearest=std::numeric_limits<double>::max();app::SelectionRef chosen;
    for(const auto& shape:window_.viewport_->ShapeViews()) {
        if(shape.surface||!window_.viewport_->EntityShown(shape.entityId))continue;
        for(const auto& triangle:shape.mesh.triangles) {
            const auto hit=modeling::RayHitsTriangle(ray->origin,ray->direction,triangle);
            if(hit && *hit<nearest){nearest=*hit;chosen.entityId=shape.entityId;chosen.pickedFaceIndex=triangle.faceIndex;}
        }
    }
    if(chosen.entityId.IsNil()){status_->setText(QStringLiteral("ソリッドのフェイスに当たりません。面の内側をクリックしてください。"));return true;}
    chosen.kind=app::SelectionElementKind::Face;chosen.hitPoint=ray->origin+ray->direction*nearest;Apply(chosen);return true;
}
void V2FaceActions::Apply(const app::SelectionRef& ref)
{
    auto& window=window_;const auto command=command_;
    const auto found=window.partShapes_.find(ref.entityId.ToString());
    if(found==window.partShapes_.end() || !ref.pickedFaceIndex){status_->setText(QStringLiteral("ソリッドのフェイスを選び直してください。"));return;}
    if(command!="workplane.from_face") {if(!Extract(ref))return;window.RunCommand(command=="fabrication.from_face" ? "fabrication.create":"fabrication.gpt_create");return;}
    const auto face=kernel::OutputFace(found->second,*ref.pickedFaceIndex);
    if(!face.HasValue()){status_->setText(QString::fromStdString(face.FirstSummaryJa()));return;}
    const auto frame=kernel::OutputSurfaceFrame(face.Value(),ref.hitPoint);
    if(!frame.HasValue()){status_->setText(QString::fromStdString(frame.FirstSummaryJa()));return;}
    app::WorkPlaneChoice choice;choice.method=modeling::WorkPlaneMethod::PointNormal;
    choice.origin=frame.Value().origin;choice.normal=frame.Value().normal;choice.uAxis=frame.Value().xDirection;
    const auto revision=window.session_->GetDocument().Revision();
    window.operationHost_->SetShelves({});window.CreateWorkPlaneFromChoice(choice,true);
    if(window.session_->GetDocument().Revision()==revision)return;
    window.SetMode(app::UiMode::Drawing);window.AlignViewToActiveWorkPlane();window.RunCommand("draw.line");
    window.SetStatus(QStringLiteral("フェイス上に作業面を作りました。曲面の場合は選択位置の接平面です。直線の始点を指定してください。"));
}
bool V2FaceActions::Extract(const app::SelectionRef& ref)
{
    auto& window=window_;
    const auto face=kernel::OutputFace(window.partShapes_.at(ref.entityId.ToString()),*ref.pickedFaceIndex);
    if(!face.HasValue()){status_->setText(QString::fromStdString(face.FirstSummaryJa()));return false;}
    const auto brep=kernel::CaptureOutputShape(face.Value());
    if(!brep.HasValue()){status_->setText(QString::fromStdString(brep.FirstSummaryJa()));return false;}
    domain::Feature feature;feature.id=window.ids_->NextTyped<base::IdKind::Feature>();
    feature.type=domain::FeatureType::FreezeDerived;feature.displayName="フェイス（近似用・独立）";
    domain::FreezeDerivedDefinition definition;definition.frozenBrep=brep.Value();feature.definition=definition;
    domain::Entity entity;entity.id=window.ids_->NextTyped<base::IdKind::Entity>();entity.createdBy=feature.id;
    entity.displayName=feature.displayName;entity.kind=domain::EntityKind::GuideSurface;entity.editPolicy=domain::EditPolicy::Frozen;
    feature.outputs.push_back({"face",entity.id,entity.kind});
    const auto added=window.session_->GetDocument().Run(document::AddFeatureCommand(feature,{entity},"近似用フェイスを取り出す"));
    if(!added.committed){window.ReportDiagnostics(added.diagnostics);return false;}
    window.operationHost_->SetShelves({});window.AdoptCurrentDocument();window.RebuildKernelShapes();window.RefreshShapeViews();
    app::SelectionSet selection;selection.entityIds={entity.id};window.viewport_->SetSelection(selection);return true;
}
