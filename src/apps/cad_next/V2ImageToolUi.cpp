#include <QImage>
#include <QString>
#include <QWidget>
#include "V2ImageTool.h"
#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"
#include <QComboBox>
#include <QCheckBox>
#include <QScrollArea>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QColor>
#include <QPen>
#include <QRectF>
#include <QPointF>
#include <QStringList>
#include <QPushButton>
#include <QVBoxLayout>
#include <functional>
using namespace kachakacha::v2;
void V2ImageTool::BuildUi() {
    auto* outer=new QVBoxLayout(this);auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);
    auto* body=new QWidget(scroll);auto* layout=new QVBoxLayout(body);scroll->setWidget(body);outer->addWidget(scroll,1);
    auto button=[&](const QString& text,const char* name,auto action){auto* b=new QPushButton(text,this);b->setObjectName(QString::fromLatin1(name));
        layout->addWidget(b);connect(b,&QPushButton::clicked,this,action);return b;};
    auto* load=button(QStringLiteral("画像ファイルを選ぶ"),"imageLoad",[this]{const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("貼る画像"),{},QStringLiteral("画像 (*.png *.jpg *.jpeg *.bmp)"));if(!path.isEmpty())LoadImage(path);});
    layout->removeWidget(load);outer->insertWidget(0,load);
    button(QStringLiteral("貼付先の面を3Dで選ぶ"),"imageTarget",[this]{Pick(1);});
    button(QStringLiteral("現在の作業面に貼る"),"imageWorkPlane",[this]{UseWorkPlane();Preview();});
    button(QStringLiteral("位置を変更：画像の基準点を3Dで選ぶ"),"imageAnchor",[this]{Pick(2);});
    button(QStringLiteral("配置の基準点を3Dで選ぶ"),"imageWorldAnchor",[this]{Pick(3);});
    button(QStringLiteral("長さ合わせ：画像の2点を3Dで選ぶ"),"imagePixelLength",[this]{pixels_.clear();points_.clear();imageMarks_.clear();Pick(4);});
    button(QStringLiteral("長さ合わせ：CADの2点を選ぶ"),"imageWorldLength",[this]{if(pixels_.size()!=2){status_->setText(QStringLiteral("先に画像の2点を指定してください。"));return;}points_.clear();Pick(5);});
    mirror_=new QCheckBox(QStringLiteral("左右反転（基準点を固定）"),this);mirror_->setObjectName(QStringLiteral("imageMirror"));
    layout->addWidget(mirror_);connect(mirror_,&QCheckBox::toggled,this,[this]{Preview();});
    auto* help=new QLabel(QStringLiteral("貼付後も画像を選択して「画像」を開くと、位置・大きさ・反転・透明度を変更できます。"),this);
    help->setWordWrap(true);layout->addWidget(help);
    auto* form=new QFormLayout;layout->addLayout(form);
    mode_=new QComboBox(this);mode_->setObjectName(QStringLiteral("imageMapping"));
    mode_->addItems({QStringLiteral("方向を指定して投影"),QStringLiteral("面に沿わせる（UV）")});form->addRow(QStringLiteral("曲面への貼り方"),mode_);
    button(QStringLiteral("投影方向を現在の作業面の法線にする"),"imageProjectionDirection",[this]{
        const auto frame=window_.WorkPlaneFrameOf(window_.ActiveWorkPlaneId());if(!frame)return;
        definition_.uAxis=frame->uAxis;definition_.vAxis=frame->vAxis;mode_->setCurrentIndex(0);Preview();});
    auto* note=new QLabel(QStringLiteral("投影方向は貼付位置の面法線、または作業面法線。面に沿わせると伸縮や継ぎ目が生じます。面形状は貼付時の独立コピーです。"),this);
    note->setWordWrap(true);layout->addWidget(note);
    auto field=[&](const QString& text,const char* name,double min,double max,double value,const QString& suffix){auto* box=new QDoubleSpinBox(this);
        box->setObjectName(QString::fromLatin1(name));box->setDecimals(5);box->setRange(min,max);box->setValue(value);box->setSuffix(suffix);form->addRow(text,box);
        connect(box,&QDoubleSpinBox::valueChanged,this,[this]{Preview();});return box;};
    width_=field(QStringLiteral("画像の幅"),"imageWidth",0.00001,10000000,100,QStringLiteral(" mm"));
    rotation_=field(QStringLiteral("回転"),"imageRotation",-360,360,0,QStringLiteral(" °"));
    opacity_=field(QStringLiteral("透明度"),"imageOpacity",0,100,0,QStringLiteral(" %"));
    status_=new QLabel(QStringLiteral("画像を選んでください。"),this);status_->setWordWrap(true);outer->addWidget(status_);
    connect(mode_,&QComboBox::currentIndexChanged,this,[this]{Preview();});layout->addStretch();layout=outer;
    button(QStringLiteral("確定 Enter"),"imageCommit",[this]{Commit();});
    button(QStringLiteral("取消 Esc"),"imageCancel",[this]{window_.operationHost_->SetShelves({});});
}
void V2ImageTool::UpdateFields() {
    updating_=true;width_->setValue(definition_.mmPerPixel*definition_.pixelWidth);
    rotation_->setValue(definition_.rotationRad*180/3.141592653589793);opacity_->setValue((1-definition_.opacity)*100);
    mirror_->setChecked(definition_.mirrorHorizontal);
    mode_->setCurrentIndex(definition_.followSurface?1:0);updating_=false;
}
void V2ImageTool::Pick(int role) {
    role_=role;
    const QStringList hints{QStringLiteral("Enterで確定できます。"),QStringLiteral("3Dビューで貼付先のフェイスをクリックしてください。"),
        QStringLiteral("3Dビューの画像上で基準点をクリックしてください。"),QStringLiteral("3Dビューで画像の基準点を置く位置をクリックしてください。"),
        QStringLiteral("3Dビューの画像上で長さを測る2点をクリックしてください。"),QStringLiteral("3Dビューで対応する長さの2点をクリックしてください（直線距離）。")};
    status_->setText(hints[role]);
}
void V2ImageTool::SetImagePoint(const geometry::Vector3& p) {
    if(image_.isNull())return;
    if(role_==2){definition_.anchorPixel=p;role_=0;}
    else if(role_==4){pixels_.push_back(p);if(pixels_.size()==2)role_=5;}
    else {status_->setText(QStringLiteral("画像の基準点か、画像の2点のボタンを押してください。"));return;}
    UpdateFields();Preview();Pick(role_);
}
