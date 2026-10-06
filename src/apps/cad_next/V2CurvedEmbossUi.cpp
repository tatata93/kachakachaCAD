#include "V2CurvedEmbossTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include "V2PanelFrame.h"
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QEvent>
#include <QObject>
#include <QString>
#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCloseEvent>
using namespace kachakacha::v2;
V2CurvedEmbossTool* V2CurvedEmbossTool::Open(V2MainWindow& window)
{
    if (auto* existing=dynamic_cast<V2CurvedEmbossTool*>(window.operationHost_->TemporaryPage())) return existing;
    const auto selection=window.viewport_->Selection();
    window.EndArmedTools(); window.ClearPendingCommand(); window.SelectTool(modeling::DrawingTool::Select);
    window.operationHost_->SetShelves({}); window.viewport_->SetSelection(selection);
    auto* tool=new V2CurvedEmbossTool(window);
    window.operationHost_->ShowTemporaryPage(tool,QStringLiteral("曲面押し出し（近似）"));
    tool->TakeSelection(); return tool;
}
V2CurvedEmbossTool::V2CurvedEmbossTool(V2MainWindow& window):QWidget(&window),window_(window),
    documentId_(window.Session().GetDocument().Snapshot().id)
{
    setObjectName(QStringLiteral("curvedEmbossPanel")); BuildUi(); qApp->installEventFilter(this);
}
void V2CurvedEmbossTool::BuildUi()
{
    auto* outer=new QVBoxLayout(this);
    auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);
    auto* body=new QWidget(scroll);auto* layout=new QVBoxLayout(body);scroll->setWidget(body);outer->addWidget(scroll);
    auto button=[&](const QString& text,auto action){auto* b=new QPushButton(text,body);layout->addWidget(b);connect(b,&QPushButton::clicked,this,action);return b;};
    layout->addWidget(MakePanelSectionTitle(body,QStringLiteral("対象")));
    sourceLabel_=new QLabel(QStringLiteral("支持面: 未選択"),body);sourceLabel_->setWordWrap(true);layout->addWidget(sourceLabel_);
    button(QStringLiteral("支持面を選ぶ"),[this]{role_=0;status_->setText(QStringLiteral("3Dビューで支持面をクリックしてください。"));});
    wireLabel_=new QLabel(QStringLiteral("輪郭: 0本"),body);layout->addWidget(wireLabel_);
    button(QStringLiteral("輪郭を追加・解除"),[this]{role_=1;status_->setText(QStringLiteral("曲面上のワイヤーをクリックしてください。もう一度押すと外れます。"));});
    button(QStringLiteral("左ペインの選択を取り込む"),[this]{TakeSelection();});
    BuildSettings(layout);
    status_=new QLabel(QStringLiteral("支持面と、その上の閉じたワイヤーを指定してください。"),this);status_->setWordWrap(true);outer->addWidget(status_);
    button(QStringLiteral("プレビューを更新"),[this]{Preview();});layout->addStretch();
    auto* cancel=new QPushButton(QStringLiteral("取消 Esc"),this);confirm_=new QPushButton(QStringLiteral("確定 Enter"),this);
    confirm_->setEnabled(false);MarkCancelConfirm(cancel,confirm_);
    auto* actions=new QHBoxLayout;actions->addWidget(cancel);actions->addWidget(confirm_);outer->addLayout(actions);
    connect(cancel,&QPushButton::clicked,this,[this]{CancelInput();});connect(confirm_,&QPushButton::clicked,this,[this]{Commit();});
}
void V2CurvedEmbossTool::BuildSettings(QVBoxLayout* layout)
{
    auto field=[&](const QString& title,const char* name,double low,double high,double initial,const QString& unit){
        layout->addWidget(new QLabel(title,this));auto* f=new QDoubleSpinBox(this);f->setObjectName(QString::fromLatin1(name));
        f->setRange(low,high);f->setDecimals(4);f->setValue(initial);f->setSuffix(unit);layout->addWidget(f);
        connect(f,&QDoubleSpinBox::valueChanged,this,[this]{Invalidate();});return f;};
    height_=field(QStringLiteral("高さ（負数で内側）"),"embossHeight",-10000,10000,1,QStringLiteral(" mm"));
    auto* reverse=new QPushButton(QStringLiteral("高さの向きを反転"),this);layout->addWidget(reverse);
    connect(reverse,&QPushButton::clicked,this,[this]{height_->setValue(-height_->value());});
    draftEnabled_=new QPushButton(QStringLiteral("抜き勾配"),this);draftEnabled_->setCheckable(true);layout->addWidget(draftEnabled_);
    draft_=field(QStringLiteral("抜き勾配角（正:先端を細く）"),"embossDraft",-79,79,0,QStringLiteral(" °"));draft_->setEnabled(false);
    tiltEnabled_=new QPushButton(QStringLiteral("斜め押し出し"),this);tiltEnabled_->setCheckable(true);layout->addWidget(tiltEnabled_);
    tilt_=field(QStringLiteral("法線からの傾き"),"embossTilt",-79,79,0,QStringLiteral(" °"));tilt_->setEnabled(false);
    heading_=field(QStringLiteral("傾ける方位（作業面のX方向が0°）"),"embossHeading",-360,360,0,QStringLiteral(" °"));heading_->setEnabled(false);
    connect(draftEnabled_,&QPushButton::toggled,this,[this](bool on){draft_->setEnabled(on);Invalidate();});
    connect(tiltEnabled_,&QPushButton::toggled,this,[this](bool on){tilt_->setEnabled(on);heading_->setEnabled(on);Invalidate();});
    tolerance_=field(QStringLiteral("輪郭の許容偏差（近似）"),"embossTolerance",0.0001,1,0.01,QStringLiteral(" mm"));
    operation_=new QComboBox(this);operation_->addItems({QStringLiteral("新しい立体"),QStringLiteral("支持面の立体に足す")});layout->addWidget(operation_);
    output_=new QComboBox(this);output_->setObjectName(QStringLiteral("embossOutput"));
    output_->addItems({QStringLiteral("面＋ワイヤー（立体）"),QStringLiteral("ワイヤーのみ"),QStringLiteral("立体のみ")});layout->addWidget(output_);
    connect(operation_,&QComboBox::currentIndexChanged,this,[this]{Invalidate();});
    connect(output_,&QComboBox::currentIndexChanged,this,[this]{Invalidate();});
    auto* note=new QLabel(QStringLiteral("輪郭の補間は近似です。確定時は独立形状として保存します。元の支持面・輪郭は残します。"),this);note->setWordWrap(true);layout->addWidget(note);
}
void V2CurvedEmbossTool::TakeSelection()
{
    const auto selection=window_.viewport_->Selection();
    for (const auto& ref:selection.ordered) {
        const auto* entity=window_.session_->GetDocument().FindEntity(ref.entityId);
        if (entity && entity->kind==domain::EntityKind::Wire) AddWire(ref.entityId); else SetSupport(ref);
    }
    if (!selection.ordered.empty()) return;
    for (const auto& id:selection.entityIds) {
        const auto* entity=window_.session_->GetDocument().FindEntity(id);
        if (entity && entity->kind==domain::EntityKind::Wire) AddWire(id);
        else { app::SelectionRef ref;ref.entityId=id;SetSupport(ref); }
    }
}
bool V2CurvedEmbossTool::eventFilter(QObject* object,QEvent* event)
{
    if (!isVisible()) return false;
    if (event->type()==QEvent::KeyPress) {
        auto* widget=qobject_cast<QWidget*>(object);if(!widget||widget->window()!=window())return false;
        const auto key=static_cast<QKeyEvent*>(event)->key();
        if(key==Qt::Key_Escape){CancelInput();return true;}
        if(key==Qt::Key_Return||key==Qt::Key_Enter){if(built_)Commit();else Preview();return true;}
    }
    if(object!=window_.viewport_||event->type()!=QEvent::MouseButtonPress)return false;
    const auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)return false;
    if(window_.viewport_->PressViewNavigator(mouse->position(),view::AxisArrowModifier::None)!=V2Viewport::ViewPress::None)return true;
    window_.viewport_->SelectAt(mouse->position(),mouse->modifiers());
    const auto picked=window_.viewport_->CurrentCandidate();if(!picked)return true;
    if(role_==1)AddWire(picked->entityId);
    else {app::SelectionRef ref;ref.entityId=picked->entityId;ref.kind=picked->kind;
        ref.pickedFaceIndex=picked->pickedFaceIndex;ref.hitPoint=picked->hitPoint;SetSupport(ref);}
    return true;
}
void V2CurvedEmbossTool::closeEvent(QCloseEvent* event)
{
    setObjectName({});window_.viewport_->HideToolPreview();QWidget::closeEvent(event);
}
