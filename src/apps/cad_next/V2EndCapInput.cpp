#include "V2EndCapInput.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2NumericSpinBox.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPointer>
#include <QString>
#include <QWidget>

using namespace kachakacha::v2;
namespace { constexpr double radians = 3.14159265358979323846 / 180.0; }
V2EndCapInput::V2EndCapInput(V2MainWindow* window,QWidget* parent):QWidget(parent),window_(window)
{
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    layout->addWidget(new QLabel(QStringLiteral("終端面の向き（正方向側）"),this));
    mode_=new QComboBox(this);mode_->setObjectName(QStringLiteral("endCapMode"));
    mode_->addItems({QStringLiteral("自動（範囲に合わせる）"),
        QStringLiteral("元の面と同じ向き"),QStringLiteral("選んだ面と平行"),QStringLiteral("角度を入力")});
    mode_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);mode_->setMinimumContentsLength(10);
    mode_->setToolTip(QStringLiteral("自動：距離指定では元面に合わせ、面まででは終端面に合わせます。曲面押し出しの元面指定は法線方向のオフセット曲面です。"));
    layout->addWidget(mode_);
    pick_=new QPushButton(QStringLiteral("向きの基準面を選ぶ"),this);
    pick_->setObjectName(QStringLiteral("endCapPick"));layout->addWidget(pick_);
    reference_=new QLabel(this);reference_->setWordWrap(true);layout->addWidget(reference_);
    tilt_=new V2NumericSpinBox(this);heading_=new V2NumericSpinBox(this);
    tilt_->setObjectName(QStringLiteral("endCapTilt"));heading_->setObjectName(QStringLiteral("endCapHeading"));
    tilt_->setRange(-89,89);heading_->setRange(-360,360);
    for(auto* field:{tilt_,heading_}) {field->setDecimals(3);field->setSuffix(QStringLiteral(" °"));layout->addWidget(field);}
    tilt_->setPrefix(QStringLiteral("傾き "));heading_->setPrefix(QStringLiteral("方位 "));
    extend_=new QCheckBox(QStringLiteral("終端の平面を延長して使う"),this);
    extend_->setObjectName(QStringLiteral("extendTargetPlane"));layout->addWidget(extend_);
    extend_->setToolTip(QStringLiteral("面までの位置指定：平面フェイスの外周外にも延長します。曲面は延長せずエラーを表示します。"));
    setToolTip(QStringLiteral("向きの基準面は選択時の向きだけを保持します。位置は距離／面までで指定。曲面の向きはクリック位置の接平面。両方向では正方向側の終端だけに適用します。"));
    const auto change=[this]{Refresh();if(!restoring_&&changed)changed();};
    connect(mode_,&QComboBox::currentIndexChanged,this,change);
    connect(tilt_,&QDoubleSpinBox::valueChanged,this,change);
    connect(heading_,&QDoubleSpinBox::valueChanged,this,change);
    connect(extend_,&QCheckBox::toggled,this,change);
    connect(pick_,&QPushButton::clicked,this,[this]{Pick();});Refresh();
}
modeling::ExtrudeEndCap V2EndCapInput::Options() const
{
    modeling::ExtrudeEndCap cap;cap.mode=static_cast<modeling::EndCapMode>(mode_->currentIndex());
    cap.referenceNormal=normal_;cap.tiltRad=tilt_->value()*radians;cap.headingRad=heading_->value()*radians;
    cap.extendTargetPlane=extend_->isChecked();return cap;
}
void V2EndCapInput::SetOptions(const modeling::ExtrudeEndCap& cap)
{
    restoring_=true;normal_=cap.referenceNormal;mode_->setCurrentIndex(static_cast<int>(cap.mode));
    tilt_->setValue(cap.tiltRad/radians);heading_->setValue(cap.headingRad/radians);
    extend_->setChecked(cap.extendTargetPlane);Refresh();restoring_=false;
}
void V2EndCapInput::SetExtentVisible(bool visible) { extend_->setVisible(visible); }
void V2EndCapInput::Refresh()
{
    const bool reference=mode_->currentIndex()==2, angle=mode_->currentIndex()==3;
    pick_->setVisible(reference);pick_->setEnabled(window_!=nullptr);reference_->setVisible(reference);
    reference_->setText(normal_.Length()>1e-9?QStringLiteral("選択時の向きを保持（位置は参照しません）"):QStringLiteral("向きの基準面が未選択です"));
    tilt_->setVisible(angle);heading_->setVisible(angle);
}
void V2EndCapInput::Pick()
{
    if(!window_)return;
    auto& w=*window_;if(w.viewport_->ReferencePickPending())w.viewport_->CancelReferencePick();
    QPointer<V2EndCapInput> self(this);
    w.viewport_->BeginElementPick([self](const app::PickCandidate& pick){
        if(!self)return false;auto& w=*self->window_;
        const auto* e=w.session_->GetDocument().FindEntity(pick.entityId);
        return e&&(e->kind==domain::EntityKind::GuideSurface||w.WorkPlaneFrameOf(pick.entityId).has_value()
            ||(e->kind==domain::EntityKind::Part&&pick.pickedFaceIndex.has_value()));
    },[self](std::optional<app::PickCandidate> pick){
        if(!self||!pick)return;auto& w=*self->window_;
        if(const auto frame=w.WorkPlaneFrameOf(pick->entityId))self->normal_=frame->normal;
        else {
            modeling::KernelShapeHandle shape;std::size_t index=pick->pickedFaceIndex.value_or(0);
            if(const auto found=w.guideShapes_.find(pick->entityId.ToString());found!=w.guideShapes_.end())shape=found->second;
            else if(const auto found=w.partShapes_.find(pick->entityId.ToString());found!=w.partShapes_.end()&&pick->pickedFaceIndex) {
                shape=found->second;index=*pick->pickedFaceIndex;
            }
            const auto pose=kernel::FacePoseNear(shape,index,pick->hitPoint,w.session_->GetDocument().Snapshot().settings.tolerance);
            if(!pose.HasValue()){w.ReportDiagnostics(pose.Diagnostics());return;}
            self->normal_=pose.Value().normal;
            w.SetStatus(pose.Value().planar?QStringLiteral("基準面と平行な終端を作ります。"):
                QStringLiteral("クリック位置の接平面と平行な終端を作ります。"));
        }
        self->Refresh();if(self->changed)self->changed();
    });
    w.SetStatus(QStringLiteral("終端の向きの基準：面・フェイス・作業平面を選んでください。Escで選択だけ取消。"));
}
