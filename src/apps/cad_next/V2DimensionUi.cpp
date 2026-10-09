#include "V2DimensionTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2PanelFrame.h"
#include "kachakacha/document/Commands.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QEvent>
#include <QObject>
#include <QString>
#include <QWidget>
using namespace kachakacha::v2;
void V2DimensionTool::BuildUi()
{
    auto* layout=new QVBoxLayout(this);
    layout->addWidget(MakePanelSectionTitle(this,QStringLiteral("寸法の対象")));
    saved_=new QComboBox(this);saved_->setObjectName(QStringLiteral("savedDimension"));layout->addWidget(saved_);
    kind_=new QComboBox(this);kind_->setObjectName(QStringLiteral("dimensionKind"));
    kind_->addItems({QStringLiteral("直線の長さ"),QStringLiteral("水平（作業面U）"),QStringLiteral("垂直（作業面V）"),
        QStringLiteral("半径 R"),QStringLiteral("直径 Ø"),QStringLiteral("2直線の角度"),QStringLiteral("2点間（端点を選択）"),QStringLiteral("2点間の水平（U）"),QStringLiteral("2点間の垂直（V）")});layout->addWidget(kind_);
    auto button=[&](const QString& text,auto action){auto* b=new QPushButton(text,this);layout->addWidget(b);connect(b,&QPushButton::clicked,this,action);};
    button(QStringLiteral("左ペインの選択を取り込む"),[this]{TakeSelection();});
    button(QStringLiteral("対象を選び直す"),[this]{Reset();});
    layout->addWidget(MakePanelSectionTitle(this,QStringLiteral("寸法値")));
    driving_=new QCheckBox(QStringLiteral("寸法値で形状を変更する（外すと参照寸法）"),this);
    driving_->setObjectName(QStringLiteral("dimensionDriving"));driving_->setChecked(true);layout->addWidget(driving_);
    value_=new QDoubleSpinBox(this);value_->setObjectName(QStringLiteral("dimensionValue"));value_->setRange(0.000001,1000000);
    value_->setDecimals(6);value_->setKeyboardTracking(false);value_->setValue(10);layout->addWidget(value_);
    name_=new QLineEdit(this);name_->setPlaceholderText(QStringLiteral("寸法の名前（任意）"));layout->addWidget(name_);
    button(QStringLiteral("3Dビューで配置位置を指定"),[this]{placing_=true;RefreshPreview();});
    button(QStringLiteral("選択した保存寸法を削除"),[this]{
        if(dimension_.id.IsNil()){status_->setText(QStringLiteral("削除する保存済み寸法を一覧か3Dの文字から選んでください。"));return;}
        const auto result=window_.session_->GetDocument().Run(document::RemoveReferenceDimensionCommand(dimension_.id));
        if(result.committed){window_.AdoptCurrentDocument();Reset();RefreshList();}
    });
    status_=new QLabel(this);status_->setWordWrap(true);layout->addWidget(status_);layout->addStretch();
    auto* cancel=new QPushButton(QStringLiteral("取消 Esc"),this);
    auto* confirm=new QPushButton(QStringLiteral("確定 Enter"),this);MarkCancelConfirm(cancel,confirm);
    auto* footer=new QHBoxLayout;footer->addWidget(cancel);footer->addWidget(confirm);layout->addLayout(footer);
    connect(cancel,&QPushButton::clicked,this,[this]{Reset();});connect(confirm,&QPushButton::clicked,this,[this]{Commit();});
    connect(kind_,&QComboBox::currentIndexChanged,this,[this]{if(!loading_)Reset();});
    connect(driving_,&QCheckBox::toggled,this,[this](bool on){value_->setEnabled(on);RefreshPreview();});
    connect(value_,&QDoubleSpinBox::valueChanged,this,[this]{RefreshPreview();});
    connect(saved_,&QComboBox::currentIndexChanged,this,[this](int index){
        if(index<=0){Reset();return;}
        if(const auto id=base::DimensionId::Parse(saved_->currentData().toString().toStdString()))Load(*id);
    });
}
bool V2DimensionTool::eventFilter(QObject* object,QEvent* event)
{
    if(!isVisible())return false;
    if(event->type()==QEvent::KeyPress) {
        auto* widget=qobject_cast<QWidget*>(object);if(!widget||widget->window()!=window())return false;
        const auto key=static_cast<QKeyEvent*>(event)->key();
        if(key==Qt::Key_Escape){Reset();return true;}
        if(key==Qt::Key_Return||key==Qt::Key_Enter){value_->interpretText();Commit();return true;}
    }
    if(object!=window_.viewport_)return false;
    if(event->type()!=QEvent::MouseButtonPress && event->type()!=QEvent::MouseMove && event->type()!=QEvent::MouseButtonDblClick)return false;
    auto* mouse=static_cast<QMouseEvent*>(event);
    if(event->type()!=QEvent::MouseMove && mouse->button()!=Qt::LeftButton)return false;
    if(event->type()==QEvent::MouseButtonDblClick) {
        if(const auto id=window_.viewport_->DimensionAt(mouse->position())){Load(*id);return true;}
    }
    if(event->type()==QEvent::MouseButtonPress
        && window_.viewport_->PressViewNavigator(mouse->position(),view::AxisArrowModifier::None)!=V2Viewport::ViewPress::None)return true;
    if(placing_ && !dimension_.anchors.empty()) {
        const auto map=window_.viewport_->Mapping();const auto ray=map.RayThrough({mouse->position().x(),mouse->position().y()});
        if(ray)dimension_.labelPosition=map.UnprojectOntoPlane({mouse->position().x(),mouse->position().y()},dimension_.anchors.front(),ray->direction);
        RefreshPreview();
        if(event->type()==QEvent::MouseButtonPress){value_->interpretText();Commit();return true;}
    } else if(event->type()==QEvent::MouseButtonPress) {
        if(const auto id=window_.viewport_->DimensionAt(mouse->position())){Load(*id);return true;}
        window_.viewport_->SelectAt(mouse->position(),mouse->modifiers());
        if(const auto picked=window_.viewport_->CurrentCandidate()) {
            app::SelectionRef ref;ref.entityId=picked->entityId;ref.segmentId=picked->segmentId;
            ref.curveParameter=picked->curveParameter;AddTarget(ref);
        }
        return true;
    }
    return false;
}
void V2DimensionTool::closeEvent(QCloseEvent* event)
{
    window_.viewport_->SetDimensionPreview({});QWidget::closeEvent(event);
}
