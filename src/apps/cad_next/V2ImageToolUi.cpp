#include <QImage>
#include <QString>
#include <QWidget>
#include "V2ImageTool.h"
#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"
#include <QComboBox>
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
class V2ImageCanvas:public QWidget {
public:
    explicit V2ImageCanvas(QWidget* parent):QWidget(parent){setMinimumHeight(150);setMaximumHeight(240);}
    QImage image;
    std::vector<geometry::Vector3> marks;
    std::function<void(geometry::Vector3)> clicked;
protected:
    QRectF ImageRect() const {
        if(image.isNull())return {};
        const double scale=std::min(double(width())/image.width(),double(height())/image.height());
        return {(width()-image.width()*scale)/2,(height()-image.height()*scale)/2,image.width()*scale,image.height()*scale};
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.fillRect(rect(),QColor(45,49,54));if(image.isNull())return;
        const auto r=ImageRect();p.drawImage(r,image);
        for(std::size_t i=0;i<marks.size();++i){const QPointF q(r.x()+marks[i].x/image.width()*r.width(),r.y()+marks[i].y/image.height()*r.height());
            p.setPen(QPen(Qt::black,4));p.drawEllipse(q,6,6);p.setPen(QPen(Qt::yellow,2));p.drawEllipse(q,6,6);
            p.drawText(q+QPointF(8,-8),i==0?QStringLiteral("基準"):QString::number(i));}
    }
    void mousePressEvent(QMouseEvent* event) override {
        const auto r=ImageRect();if(event->button()!=Qt::LeftButton||!r.contains(event->position())||!clicked)return;
        clicked({(event->position().x()-r.x())/r.width()*image.width(),(event->position().y()-r.y())/r.height()*image.height(),0});
    }
};
void V2ImageTool::BuildUi() {
    auto* outer=new QVBoxLayout(this);auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);
    auto* body=new QWidget(scroll);auto* layout=new QVBoxLayout(body);scroll->setWidget(body);outer->addWidget(scroll,1);
    auto button=[&](const QString& text,const char* name,auto action){auto* b=new QPushButton(text,this);b->setObjectName(QString::fromLatin1(name));
        layout->addWidget(b);connect(b,&QPushButton::clicked,this,action);return b;};
    auto* load=button(QStringLiteral("画像ファイルを選ぶ"),"imageLoad",[this]{const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("貼る画像"),{},QStringLiteral("画像 (*.png *.jpg *.jpeg *.bmp)"));if(!path.isEmpty())LoadImage(path);});
    layout->removeWidget(load);outer->insertWidget(0,load);
    canvas_=new V2ImageCanvas(this);canvas_->setObjectName(QStringLiteral("imagePointCanvas"));outer->insertWidget(1,canvas_);
    canvas_->clicked=[this](const geometry::Vector3& p){SetImagePoint(p);};
    button(QStringLiteral("貼付先の面を3Dで選ぶ"),"imageTarget",[this]{Pick(1);});
    button(QStringLiteral("現在の作業面に貼る"),"imageWorkPlane",[this]{UseWorkPlane();Preview();});
    button(QStringLiteral("画像の基準点を選ぶ ↑"),"imageAnchor",[this]{Pick(2);});
    button(QStringLiteral("配置の基準点を3Dで選ぶ"),"imageWorldAnchor",[this]{Pick(3);});
    button(QStringLiteral("長さ合わせ：画像の2点を選ぶ ↑"),"imagePixelLength",[this]{pixels_.clear();points_.clear();Pick(4);});
    button(QStringLiteral("長さ合わせ：CADの2点を選ぶ"),"imageWorldLength",[this]{if(pixels_.size()!=2){status_->setText(QStringLiteral("先に画像の2点を指定してください。"));return;}points_.clear();Pick(5);});
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
    opacity_=field(QStringLiteral("不透明度"),"imageOpacity",0,100,100,QStringLiteral(" %"));
    status_=new QLabel(QStringLiteral("画像を選んでください。"),this);status_->setWordWrap(true);outer->addWidget(status_);
    connect(mode_,&QComboBox::currentIndexChanged,this,[this]{Preview();});layout->addStretch();layout=outer;
    button(QStringLiteral("確定 Enter"),"imageCommit",[this]{Commit();});
    button(QStringLiteral("取消 Esc"),"imageCancel",[this]{window_.operationHost_->SetShelves({});});
}
void V2ImageTool::UpdateFields() {
    updating_=true;width_->setValue(definition_.mmPerPixel*definition_.pixelWidth);
    rotation_->setValue(definition_.rotationRad*180/3.141592653589793);opacity_->setValue(definition_.opacity*100);
    mode_->setCurrentIndex(definition_.followSurface?1:0);canvas_->image=image_;canvas_->marks={definition_.anchorPixel};
    canvas_->marks.insert(canvas_->marks.end(),pixels_.begin(),pixels_.end());canvas_->update();updating_=false;
}
void V2ImageTool::Pick(int role) {
    role_=role;
    const QStringList hints{QStringLiteral("Enterで確定できます。"),QStringLiteral("3Dビューで貼付先のフェイスをクリックしてください。"),
        QStringLiteral("上の画像で基準点をクリックしてください。"),QStringLiteral("3Dビューで画像の基準点を置く位置をクリックしてください。"),
        QStringLiteral("上の画像で長さを測る2点をクリックしてください。"),QStringLiteral("3Dビューで対応する長さの2点をクリックしてください（直線距離）。")};
    status_->setText(hints[role]);
}
void V2ImageTool::SetImagePoint(const geometry::Vector3& p) {
    if(image_.isNull())return;
    if(role_==2){definition_.anchorPixel=p;role_=0;}
    else if(role_==4){pixels_.push_back(p);if(pixels_.size()==2)role_=5;}
    else {status_->setText(QStringLiteral("画像の基準点か、画像の2点のボタンを押してください。"));return;}
    UpdateFields();Preview();Pick(role_);
}
