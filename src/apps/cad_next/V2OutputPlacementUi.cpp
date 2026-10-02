#include <QObject>
#include <QString>
#include <QWidget>
#include "V2OutputTool.h"
#include "V2MainWindow.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/modeling/MeshPick.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <limits>
using namespace kachakacha::v2;
namespace {
geometry::Vector3 Vector(const std::array<QDoubleSpinBox*,3>& boxes){return {boxes[0]->value(),boxes[1]->value(),boxes[2]->value()};}
void Values(const std::array<QDoubleSpinBox*,3>& boxes,geometry::Vector3 v){boxes[0]->setValue(v.x);boxes[1]->setValue(v.y);boxes[2]->setValue(v.z);}
}
void V2OutputTool::AddVectorRow(QFormLayout* layout,const QString& label,std::array<QDoubleSpinBox*,3>& values,const geometry::Vector3& initial) {
    auto* row=new QWidget(this);auto* fields=new QHBoxLayout(row);fields->setContentsMargins(0,0,0,0);
    for(auto& value:values){value=new QDoubleSpinBox(row);value->setRange(-1e9,1e9);value->setDecimals(6);value->setMaximumWidth(105);fields->addWidget(value);}
    Values(values,initial);layout->addRow(label,row);
    for(auto* value:values)QObject::connect(value,&QDoubleSpinBox::valueChanged,this,[this]{RefreshPreview();});
}
void V2OutputTool::BuildPlacement(QVBoxLayout* layout) {
    placement_=new QComboBox(this);placement_->setObjectName("outputPlacement");placement_->addItems({QStringLiteral("元の位置を維持"),QStringLiteral("基準点を面へ配置")});layout->addWidget(placement_);
    placementFields_=new QWidget(this);auto* fields=new QVBoxLayout(placementFields_);fields->setContentsMargins(0,0,0,0);
    auto* form=new QFormLayout();AddVectorRow(form,QStringLiteral("元の基準点 XYZ"),sourcePoint_,{});AddVectorRow(form,QStringLiteral("元の法線 XYZ"),sourceNormal_,{0,0,1});AddVectorRow(form,QStringLiteral("元の面内X方向"),sourceX_,{1,0,0});
    AddVectorRow(form,QStringLiteral("配置点 XYZ"),targetPoint_,{});AddVectorRow(form,QStringLiteral("配置面の法線"),targetNormal_,{0,0,1});AddVectorRow(form,QStringLiteral("配置面のX方向"),targetX_,{1,0,0});fields->addLayout(form);
    const std::pair<QString,int> picks[]{{QStringLiteral("基準点を3Dで指定"),1},{QStringLiteral("元の向きを面から指定"),2},{QStringLiteral("配置先を面上で指定"),3},{QStringLiteral("選択中の作業平面へ配置"),4}};
    for(const auto& [label,role]:picks){auto* button=new QPushButton(label,this);fields->addWidget(button);QObject::connect(button,&QPushButton::clicked,this,[this,role]{Pick(role);});}
    fields->addWidget(new QLabel(QStringLiteral("平面・曲面の配置点をクリック。曲面は接平面の向きを使います。"),this));layout->addWidget(placementFields_);placementFields_->hide();
    QObject::connect(placement_,&QComboBox::currentIndexChanged,this,[this](int i){placementFields_->setVisible(i==1);RefreshPreview();});
}
geometry::OutputPlacement V2OutputTool::Placement() const {
    geometry::OutputPlacement value;value.keepPosition=placement_->currentIndex()==0;
    value.source={Vector(sourcePoint_),Vector(sourceNormal_),Vector(sourceX_)};value.destination={Vector(targetPoint_),Vector(targetNormal_),Vector(targetX_)};return value;
}
void V2OutputTool::SetPlacement(const geometry::OutputPlacement& value){loading_=true;placement_->setCurrentIndex(value.keepPosition?0:1);
    Values(sourcePoint_,value.source.origin);Values(sourceNormal_,value.source.normal);Values(sourceX_,value.source.xDirection);
    Values(targetPoint_,value.destination.origin);Values(targetNormal_,value.destination.normal);Values(targetX_,value.destination.xDirection);loading_=false;RefreshPreview();}
void V2OutputTool::SetPickedFrame(int role,const geometry::OutputFrame& frame) {
    auto value=Placement();value.keepPosition=false;
    if(role==1)value.source.origin=frame.origin;
    else if(role==2){value.source.normal=frame.normal;value.source.xDirection=frame.xDirection;}
    else value.destination=frame;
    pickRole_=0;SetPlacement(value);show();raise();
}
void V2OutputTool::Pick(int role) {
    if(pickRole_==1)window_.viewport_->CancelPointPick();pickRole_=0;
    if(role==4){for(const auto& id:window_.viewport_->Selection().entityIds){const auto frame=window_.WorkPlaneFrameOf(id);if(frame.has_value()){SetPickedFrame(3,{frame->origin,frame->normal,frame->uAxis});return;}}
        status_->setText(QStringLiteral("左ペインか3Dで配置先の作業平面を選んでください。"));return;}
    pickRole_=role;status_->setText(QStringLiteral("CADの3Dビューで%1をクリックしてください。").arg(role==1?QStringLiteral("基準点"):QStringLiteral("面上の点")));
    if(role==1){QPointer<V2OutputTool> guard(this);window_.viewport_->BeginPointPick([guard](const V2Viewport::PickedPoint& point){if(guard)guard->SetPickedFrame(1,{point.point,{0,0,1},{1,0,0}});},"出力の基準点を指定してください。");}
}
bool V2OutputTool::eventFilter(QObject*,QEvent* event) {
    if(pickRole_>0 && event->type()==QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){if(pickRole_==1)window_.viewport_->CancelPointPick();pickRole_=0;RefreshPreview();show();raise();return true;}
    if(pickRole_<1 || event->type()!=QEvent::MouseButtonPress)return false;
    const auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)return false;
    const auto ray=window_.viewport_->Mapping().RayThrough({mouse->position().x(),mouse->position().y()});if(!ray.has_value())return true;
    double nearest=std::numeric_limits<double>::max();const V2Viewport::ShapeView* chosen=nullptr;std::size_t faceIndex=0;
    for(const auto& shape:window_.viewport_->ShapeViews())for(const auto& triangle:shape.mesh.triangles){const auto hit=modeling::RayHitsTriangle(ray->origin,ray->direction,triangle);if(hit.has_value()&&*hit<nearest){nearest=*hit;chosen=&shape;faceIndex=triangle.faceIndex;}}
    if(chosen==nullptr){if(pickRole_==1)return false;status_->setText(QStringLiteral("面に当たりません。作業平面は選択後に専用ボタンで指定できます。"));return true;}
    const auto& shapes=chosen->surface?window_.guideShapes_:window_.partShapes_;const auto found=shapes.find(chosen->entityId.ToString());if(found==shapes.end())return true;
    const auto face=kernel::OutputFace(found->second,faceIndex);if(!face.HasValue()){status_->setText(QStringLiteral("指定した面を評価できません。"));return true;}
    const auto frame=kernel::OutputSurfaceFrame(face.Value(),ray->origin+ray->direction*nearest);
    if(!frame.HasValue()){status_->setText(QString::fromStdString(frame.FirstSummaryJa()));return true;}
    if(pickRole_==1)window_.viewport_->CancelPointPick();
    SetPickedFrame(pickRole_,frame.Value());return true;
}
