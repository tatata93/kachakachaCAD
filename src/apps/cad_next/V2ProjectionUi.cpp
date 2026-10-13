#include "V2ProjectionTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include "V2PanelFrame.h"
#include "V2NumericSpinBox.h"
#include "V2EntityTree.h"
#include "kachakacha/modeling/MeshPick.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <QApplication>
#include <QAbstractItemModel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QObject>
#include <QPointF>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <limits>
#include <algorithm>
using namespace kachakacha::v2;
V2ProjectionTool* V2ProjectionTool::Open(V2MainWindow& window,bool wrap) {
    if(auto* current=dynamic_cast<V2ProjectionTool*>(window.operationHost_->TemporaryPage())) {
        current->mode_->setCurrentIndex(wrap?1:0);return current;
    }
    const auto selection=window.viewport_->Selection();window.EndArmedTools();window.ClearPendingCommand();
    window.SelectTool(modeling::DrawingTool::Select);window.operationHost_->SetShelves({});
    window.viewport_->SetSelection(selection);auto* tool=new V2ProjectionTool(window);
    window.operationHost_->ShowTemporaryPage(tool,QStringLiteral("曲面へ投影"));
    tool->mode_->setCurrentIndex(wrap?1:0);tool->TakeSelection();return tool;
}
V2ProjectionTool::V2ProjectionTool(V2MainWindow& window):QWidget(&window),window_(window),
    documentId_(window.Session().GetDocument().Snapshot().id),revision_(window.Session().GetDocument().Revision()) {
    setObjectName(QStringLiteral("projectionPanel"));
    sourceU_=window.viewport_->WorkPlane().uAxis;sourceV_=window.viewport_->WorkPlane().vAxis;
    update_=new QTimer(this);update_->setSingleShot(true);update_->setInterval(250);
    connect(update_,&QTimer::timeout,this,[this]{Preview();});BuildUi();qApp->installEventFilter(this);
    connect(window_.entityTree_,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem*,int){
        if(isVisible()&&(role_==0||role_==1))TakeActiveSelection();});
    auto* marks=new QTimer(this);marks->setSingleShot(true);marks->setInterval(0);
    connect(window_.entityTree_->model(),&QAbstractItemModel::rowsInserted,this,[marks]{marks->start();});
    connect(marks,&QTimer::timeout,this,[this]{if(isVisible())RefreshMarks();});
}
void V2ProjectionTool::BuildUi() {
    auto* outer=new QVBoxLayout(this);auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);
    auto* body=new QWidget(scroll);auto* layout=new QVBoxLayout(body);scroll->setWidget(body);outer->addWidget(scroll);
    auto button=[&](const QString& text,auto action){auto* b=new QPushButton(text,body);layout->addWidget(b);connect(b,&QPushButton::clicked,this,action);return b;};
    mode_=new QComboBox(body);mode_->setObjectName(QStringLiteral("projectionMode"));
    mode_->addItems({QStringLiteral("図面から投影（方向指定）"),QStringLiteral("曲面に巻き付け（等長）"),QStringLiteral("3Dワイヤーを面に載せる（最近点）")});layout->addWidget(mode_);
    layout->addWidget(MakePanelSectionTitle(body,QStringLiteral("入力")));
    auto* legend=new QLabel(QStringLiteral("<b style='color:#963ccd'>紫: 元ワイヤー</b> → <b style='color:#1ea046'>緑: 投影先</b><br>水色の破線: 投影結果（未確定）"),body);
    legend->setObjectName(QStringLiteral("projectionLegend"));layout->addWidget(legend);
    pickPrompt_=new QLabel(body);pickPrompt_->setObjectName(QStringLiteral("projectionPickPrompt"));pickPrompt_->setWordWrap(true);layout->addWidget(pickPrompt_);
    pickWire_=button(QStringLiteral("① 元ワイヤーを選ぶ"),[this]{SetPickRole(0);});
    pickWire_->setObjectName(QStringLiteral("projectionPickWire"));pickWire_->setCheckable(true);
    wireRows_=new QListWidget(body);wireRows_->setObjectName(QStringLiteral("projectionWireRows"));wireRows_->setMaximumHeight(110);layout->addWidget(wireRows_);
    button(QStringLiteral("選択行のワイヤーを解除"),[this]{const int i=wireRows_->currentRow();if(i>=0){sources_.erase(sources_.begin()+i);RefreshRows();Invalidate();}});
    pickTarget_=button(QStringLiteral("② 投影先を選ぶ"),[this]{SetPickRole(1);});
    pickTarget_->setObjectName(QStringLiteral("projectionPickTarget"));pickTarget_->setCheckable(true);
    targetRows_=new QListWidget(body);targetRows_->setObjectName(QStringLiteral("projectionTargetRows"));targetRows_->setMaximumHeight(100);layout->addWidget(targetRows_);
    button(QStringLiteral("選択行の面を解除"),[this]{const int i=targetRows_->currentRow();if(i>=0){kernel::ReleaseShape(targets_[i].shape);targets_.erase(targets_.begin()+i);RefreshRows();Invalidate();}});
    button(QStringLiteral("左ペインの選択を現在の役割へ追加"),[this]{TakeActiveSelection();});
    output_=new QComboBox(body);output_->setObjectName(QStringLiteral("projectionOutput"));
    output_->addItems({QStringLiteral("出力: ワイヤーのみ"),QStringLiteral("出力: 面＋ワイヤー（閉領域を選択面へ）")});layout->addWidget(output_);
    auto* outputHelp=new QLabel(QStringLiteral("面＋ワイヤー: 閉じた平面輪郭を指定方向に投影し、選択したすべてのフェイスの曲率・穴を保ちます。手前判定はワイヤーのみで使用します。"),body);
    outputHelp->setWordWrap(true);outputHelp->setProperty("projectionOnlyMode",0);layout->addWidget(outputHelp);
    connect(output_,&QComboBox::currentIndexChanged,this,[this]{hits_->setVisible(mode_->currentIndex()==0&&output_->currentIndex()==0);Invalidate();});
    BuildSettings(layout);
    button(QStringLiteral("プレビューを更新"),[this]{Preview();});layout->addStretch();
    status_=new QLabel(QStringLiteral("元ワイヤーと投影先を選び、プレビューを確認してEnterで確定。"),this);status_->setObjectName(QStringLiteral("projectionStatus"));status_->setWordWrap(true);status_->setMinimumHeight(68);outer->addWidget(status_);
    confirm_=new QPushButton(QStringLiteral("確定 Enter"),this);confirm_->setObjectName(QStringLiteral("projectionConfirm"));confirm_->setEnabled(false);
    auto* cancel=new QPushButton(QStringLiteral("取消 Esc"),this);MarkCancelConfirm(cancel,confirm_);
    auto* actions=new QHBoxLayout;actions->addWidget(cancel);actions->addWidget(confirm_);outer->addLayout(actions);
    connect(cancel,&QPushButton::clicked,this,[this]{CancelInput();});connect(confirm_,&QPushButton::clicked,this,[this]{Commit();});
    auto* back=new QPushButton(QStringLiteral("ツール一覧に戻る"),this);outer->addWidget(back);
    connect(back,&QPushButton::clicked,this,[this]{window_.RunCommand("selection.activate");});
    connect(mode_,&QComboBox::currentIndexChanged,this,[this]{RefreshModeUi();Invalidate();});RefreshModeUi();
}
void V2ProjectionTool::RefreshModeUi() {
    const int mode=mode_->currentIndex();SetPickRole(0);
    if(mode!=0)output_->setCurrentIndex(0);output_->setEnabled(mode==0);
    for(auto* widget:findChildren<QWidget*>()) {
        const auto setting=widget->property("projectionOnlyMode");if(!setting.isValid())continue;
        widget->setVisible(setting.toInt()==mode||(setting.toInt()==-1&&mode!=1));
    }
    rotation_->setEnabled(mode==1);RefreshRows();
    hits_->setVisible(mode==0&&output_->currentIndex()==0);
}
void V2ProjectionTool::BuildSettings(QVBoxLayout* layout) {
    layout->addWidget(MakePanelSectionTitle(this,QStringLiteral("設定")));
    hits_=new QComboBox(this);hits_->setObjectName(QStringLiteral("projectionHits"));
    hits_->addItems({QStringLiteral("向きに沿った最初の面（手前）"),QStringLiteral("両方向のうち最も近い面"),QStringLiteral("両方向の全交点")});layout->addWidget(hits_);
    hits_->setProperty("projectionOnlyMode",0);
    connect(hits_,&QComboBox::currentIndexChanged,this,[this]{Invalidate();});
    auto* note=new QLabel(QStringLiteral("方向は元ワイヤーの選択行に適用します。行未選択なら全入力に適用。"),this);note->setWordWrap(true);layout->addWidget(note);
    note->setProperty("projectionOnlyMode",0);
    auto* allDirections=new QPushButton(QStringLiteral("方向の設定対象: 元ワイヤーすべて"),this);allDirections->setProperty("projectionOnlyMode",0);layout->addWidget(allDirections);
    connect(allDirections,&QPushButton::clicked,this,[this]{wireRows_->setCurrentRow(-1);wireRows_->clearSelection();});
    auto direction=[&](const QString& text,geometry::Vector3 d){auto* b=new QPushButton(text,this);b->setProperty("projectionOnlyMode",0);connect(b,&QPushButton::clicked,this,[this,d]{SetDirection(d);});return b;};
    auto* axes=new QHBoxLayout;axes->addWidget(direction(QStringLiteral("X方向"),{1,0,0}));axes->addWidget(direction(QStringLiteral("Y方向"),{0,1,0}));axes->addWidget(direction(QStringLiteral("Z方向"),{0,0,1}));layout->addLayout(axes);
    auto* normal=new QPushButton(QStringLiteral("作業平面の法線方向"),this);layout->addWidget(normal);
    normal->setProperty("projectionOnlyMode",0);
    connect(normal,&QPushButton::clicked,this,[this]{SetDirection(-window_.viewport_->WorkPlane().normal);});
    auto* reverse=new QPushButton(QStringLiteral("方向を反転"),this);layout->addWidget(reverse);
    reverse->setProperty("projectionOnlyMode",0);
    connect(reverse,&QPushButton::clicked,this,[this]{const int row=wireRows_->currentRow();for(std::size_t i=0;i<sources_.size();++i)if(row<0||i==static_cast<std::size_t>(row)){sources_[i].input.direction=-sources_[i].input.direction;sources_[i].directionExplicit=true;}RefreshRows();Invalidate();});
    auto pick=[&](const QString& text,int role){auto* b=new QPushButton(text,this);b->setProperty("projectionOnlyMode",role==4?0:1);layout->addWidget(b);connect(b,&QPushButton::clicked,this,[this,role]{SetPickRole(role);});};
    pick(QStringLiteral("線で投影方向を指定"),4);pick(QStringLiteral("巻き付け: 元ワイヤーの基準点"),2);pick(QStringLiteral("巻き付け: 面上の配置基準点"),3);
    auto* plane=new QPushButton(QStringLiteral("巻き付け元の平面を現在の作業面にする"),this);layout->addWidget(plane);
    plane->setProperty("projectionOnlyMode",1);
    connect(plane,&QPushButton::clicked,this,[this]{sourceU_=window_.viewport_->WorkPlane().uAxis;sourceV_=window_.viewport_->WorkPlane().vAxis;Invalidate();});
    auto* detailsButton=new QToolButton(this);detailsButton->setText(QStringLiteral("詳細（許容偏差・最大距離・巻き付け角）"));detailsButton->setCheckable(true);detailsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);detailsButton->setArrowType(Qt::RightArrow);layout->addWidget(detailsButton);
    auto* details=new QWidget(this);auto* fields=new QVBoxLayout(details);fields->setContentsMargins(0,0,0,0);layout->addWidget(details);details->hide();
    connect(detailsButton,&QToolButton::toggled,this,[details,detailsButton](bool expanded){details->setVisible(expanded);detailsButton->setArrowType(expanded?Qt::DownArrow:Qt::RightArrow);});
    auto field=[&](const QString& name,const char* id,double low,double high,double value,const QString& unit){auto* label=new QLabel(name,details);fields->addWidget(label);auto* s=new V2NumericSpinBox(details);s->setObjectName(QString::fromLatin1(id));s->setDecimals(6);s->setRange(low,high);s->setValue(value);s->setSuffix(unit);fields->addWidget(s);if(QString::fromLatin1(id)!="projectionTolerance"){const int mode=QString::fromLatin1(id)=="projectionRotation"?1:-1;label->setProperty("projectionOnlyMode",mode);s->setProperty("projectionOnlyMode",mode);}connect(s,&QDoubleSpinBox::valueChanged,this,[this]{Invalidate();});return s;};
    tolerance_=field(QStringLiteral("許容偏差"),"projectionTolerance",0.000001,1,0.001,QStringLiteral(" mm"));
    distance_=field(QStringLiteral("最大投影距離"),"projectionDistance",0.000001,1000000,10000,QStringLiteral(" mm"));
    rotation_=field(QStringLiteral("巻き付けの向き"),"projectionRotation",-360,360,0,QStringLiteral(" °"));rotation_->setEnabled(false);
    auto* help=new QLabel(QStringLiteral("巻き付けは1枚の平面・円柱・円錐で長さを保ち、面内に収まる輪郭に使います。方向投影／最近点投影は複数面に対応し、面外・穴で切り取ります。離れた端点は橋渡しせず、元形状は残ります。"),this);help->setWordWrap(true);layout->addWidget(help);
}
void V2ProjectionTool::TakeSelection() {
    const auto selection=window_.viewport_->Selection();
    auto take=[&](const app::SelectionRef& ref){const auto* entity=window_.session_->GetDocument().FindEntity(ref.entityId);if(entity&&entity->kind==domain::EntityKind::Wire)AddWire(ref,false);else AddTarget(ref,false);};
    for(const auto& ref:selection.ordered)take(ref);
    for(const auto& id:selection.entityIds)if(std::none_of(selection.ordered.begin(),selection.ordered.end(),[&](const auto& ref){return ref.entityId==id;})){app::SelectionRef ref;ref.entityId=id;take(ref);}
}
void V2ProjectionTool::PickViewport(const QPointF& position) {
    if(role_==1||role_==3) {
        const auto ray=window_.viewport_->Mapping().RayThrough({position.x(),position.y()});if(!ray)return;
        double closest=std::numeric_limits<double>::max();app::SelectionRef picked;
        for(const auto& shape:window_.viewport_->ShapeViews()) {
            if(!window_.viewport_->EntityShown(shape.entityId))continue;
            for(const auto& triangle:shape.mesh.triangles) {
                const auto hit=modeling::RayHitsTriangle(ray->origin,ray->direction,triangle);
                if(hit&&*hit<closest){closest=*hit;picked.entityId=shape.entityId;picked.pickedFaceIndex=shape.surface?std::nullopt:std::optional<std::size_t>{triangle.faceIndex};}
            }
        }
        if(picked.entityId.IsNil()){status_->setText(QStringLiteral("面の内側をクリックしてください。"));return;}
        picked.kind=app::SelectionElementKind::Face;picked.hitPoint=ray->origin+ray->direction*closest;
        if(role_==1)AddTarget(picked);
        else {
            if(!targets_.empty()&&std::none_of(targets_.begin(),targets_.end(),[&](const auto& t){return t.ref.entityId==picked.entityId&&t.ref.pickedFaceIndex==picked.pickedFaceIndex;})) {
                status_->setText(QStringLiteral("選択済みの投影先の面をクリックしてください。"));return;
            }
            if(AddTarget(picked,false)){targetPoint_=picked.hitPoint;Invalidate();}
        }
        return;
    }
    window_.viewport_->SelectAt(position,Qt::NoModifier);const auto candidate=window_.viewport_->CurrentCandidate();if(!candidate)return;
    app::SelectionRef ref;ref.entityId=candidate->entityId;ref.kind=candidate->kind;
    ref.segmentId=candidate->segmentId;ref.hitPoint=candidate->hitPoint;
    if(role_==0) {ref.segmentId.reset();AddWire(ref);}
    else if(role_==2) {sourceOrigin_=ref.hitPoint;Invalidate();}
    else if(role_==4) {
        app::SelectionSet selection;selection.entityIds={ref.entityId};selection.ordered={ref};
        const auto curves=app::SelectedCurves(selection,window_.session_->Scene());
        if(!curves.empty()&&curves.front().Kind()==geometry::CurveKind::Line)SetDirection(curves.front().EndPoint()-curves.front().StartPoint());
        else status_->setText(QStringLiteral("投影方向には直線を選んでください。"));
    }
}
bool V2ProjectionTool::eventFilter(QObject* object,QEvent* event) {
    if(!isVisible())return false;
    if(event->type()==QEvent::KeyPress) {
        auto* widget=qobject_cast<QWidget*>(object);if(!widget||widget->window()!=window())return false;
        const int key=static_cast<QKeyEvent*>(event)->key();
        if(key==Qt::Key_Escape){CancelInput();return true;}
        if(key==Qt::Key_Return||key==Qt::Key_Enter) {
            auto* field=dynamic_cast<QDoubleSpinBox*>(object);if(!field)field=dynamic_cast<QDoubleSpinBox*>(object->parent());
            if(field){if(!field->hasAcceptableInput())return true;const auto before=field->value();field->interpretText();if(field->value()!=before){Preview();return true;}}
            if(result_)Commit();else Preview();return true;
        }
    }
    if(object!=window_.viewport_||event->type()!=QEvent::MouseButtonPress)return false;
    const auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)return false;
    if(window_.viewport_->PressViewNavigator(mouse->position(),view::AxisArrowModifier::None)!=V2Viewport::ViewPress::None)return true;
    PickViewport(mouse->position());return true;
}
void V2ProjectionTool::closeEvent(QCloseEvent* event) {
    update_->stop();qApp->removeEventFilter(this);ClearMarks();window_.viewport_->HideToolPreview();setObjectName({});QWidget::closeEvent(event);
}
