#include "V2InstructionMode.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QGraphicsTextItem>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QMessageBox>
#include <QPainterPath>
#include <QJsonArray>
#include <cmath>
#include <QPainter>
#include <QLineF>
#include <QFont>
#include <algorithm>
#include <QColor>
#include <QEvent>
#include <QGraphicsItem>
#include <QObject>
#include <QPen>
#include <QPointF>
#include <QResizeEvent>
#include <QString>
#include <QVariant>
#include <QWidget>
using kachakacha::v2::app::UiMode;
namespace {
V2InstructionMode* Find(V2MainWindow& window){
    for(auto* child:window.centralWidget()->children())
        if(auto* mode=dynamic_cast<V2InstructionMode*>(child))return mode;
    return nullptr;
}
}
void V2InstructionMode::Sync(V2MainWindow& window){
    auto* mode=Find(window);const bool active=window.Mode()==UiMode::Instructions;
    if(!mode&&active)mode=new V2InstructionMode(window);
    if(!mode)return;
    mode->setVisible(active);window.Viewport().setVisible(!active);
    if(active){mode->ShowSettings();mode->view_->fitInView(mode->view_->sceneRect(),Qt::KeepAspectRatio);}
    else if(mode->settings_)window.OperationHost().ClearTemporaryPage();
}
void V2InstructionMode::SetPathChooser(V2MainWindow& window,std::function<QString(bool)> chooser){
    auto* mode=Find(window);if(!mode){mode=new V2InstructionMode(window);mode->hide();}
    mode->pathChooser_=std::move(chooser);
}
bool V2InstructionMode::Run(V2MainWindow& window,std::string_view id){
    if(id.starts_with("instructions.")){
        if(window.Mode()!=UiMode::Instructions)window.SetMode(UiMode::Instructions);
        Find(window)->Command(id);return true;
    }
    if(window.Mode()!=UiMode::Instructions)return false;
    auto* mode=Find(window);if(!mode)return false;
    if(id=="edit.undo")mode->Undo(false);
    else if(id=="edit.redo")mode->Undo(true);
    else if(id=="edit.delete")mode->Command("instructions.remove");
    else if(id=="file.save"||id=="file.save_as")mode->FileAction("instructions.save");
    else if(id=="file.open")mode->FileAction("instructions.open");
    else if(id=="selection.activate")mode->Command("instructions.move");
    else if(id=="tools.search"){window.OperationHost().SetShelves({});window.OperationHost().FocusToolSearch();}
    else if(id=="view.fit_all")mode->view_->fitInView(mode->view_->sceneRect(),Qt::KeepAspectRatio);
    else if(mode->hint_)mode->hint_->setText(QStringLiteral("説明書モードでは説明図を編集します。CADの操作は作図・部品モードに戻ってください。"));
    return true;
}
V2InstructionMode::V2InstructionMode(V2MainWindow& window):QWidget(window.centralWidget()),window_(window){
    setObjectName(QStringLiteral("instructionWorkspace"));
    auto* layout=new QVBoxLayout(this);auto* row=new QHBoxLayout;layout->addLayout(row);
    pages_=new QComboBox(this);pages_->setObjectName(QStringLiteral("instructionPages"));row->addWidget(pages_);
    title_=new QLineEdit(this);title_->setPlaceholderText(QStringLiteral("手順の見出し"));row->addWidget(title_,1);
    view_=new QGraphicsView(this);view_->setObjectName(QStringLiteral("instructionCanvas"));
    view_->setRenderHint(QPainter::Antialiasing);view_->setBackgroundBrush(Qt::white);
    view_->setDragMode(QGraphicsView::RubberBandDrag);view_->setMouseTracking(true);layout->addWidget(view_,1);
    static_cast<QVBoxLayout*>(window.centralWidget()->layout())->insertWidget(0,this,1);
    qApp->installEventFilter(this);
    connect(pages_,&QComboBox::currentIndexChanged,this,[this](int i){if(!loading_)ShowPage(i);});
    connect(title_,&QLineEdit::textEdited,this,[this]{if(loading_)return;before_=Snapshot();titles_[current_]=title_->text();
        pages_->setItemText(current_,QString::number(current_+1)+QStringLiteral("：")+titles_[current_]);Remember();});
    NewPage(false);dirty_=false;undo_.clear();
}
void V2InstructionMode::NewPage(bool copy){
    CancelArrow();before_=Snapshot();const auto previous=current_;
    auto* scene=new QGraphicsScene(this);scene->setSceneRect(0,0,1120,792);scene->setBackgroundBrush(Qt::white);
    scenes_.push_back(scene);titles_.push_back(QStringLiteral("手順 %1").arg(scenes_.size()));
    loading_=true;pages_->addItem(QString::number(scenes_.size())+QStringLiteral("：")+titles_.back());loading_=false;
    ShowPage(static_cast<int>(scenes_.size())-1);
    if(copy&&!before_.isEmpty()){
        auto doc=Snapshot();auto pages=doc["pages"].toArray();auto last=pages.last().toObject();
        last["items"]=pages[previous].toObject()["items"];pages[pages.size()-1]=last;doc["pages"]=pages;Restore(doc);
    }
    Remember();
}
void V2InstructionMode::ShowPage(int index){
    if(index<0||index>=static_cast<int>(scenes_.size()))return;
    CancelArrow();arrowTool_=false;current_=index;loading_=true;pages_->setCurrentIndex(index);title_->setText(titles_[index]);loading_=false;
    view_->setScene(scenes_[index]);view_->fitInView(view_->sceneRect(),Qt::KeepAspectRatio);
}
void V2InstructionMode::ShowSettings(){
    auto* panel=new QWidget;settings_=panel;auto* layout=new QVBoxLayout(panel);
    hint_=new QLabel(QStringLiteral("モデルで視点を決める → 説明書で部品を取り込む → ドラッグで分解図を配置。矢印は始点・終点の2点指定。"),panel);
    hint_->setWordWrap(true);layout->addWidget(hint_);
    auto* text=new QLineEdit(panel);text->setObjectName(QStringLiteral("instructionText"));text->setPlaceholderText(QStringLiteral("番号や説明文"));layout->addWidget(text);
    auto* apply=new QPushButton(QStringLiteral("選択した文字を更新"),panel);layout->addWidget(apply);
    connect(apply,&QPushButton::clicked,this,[this,text]{before_=Snapshot();for(auto* item:scenes_[current_]->selectedItems())
        if(auto* label=dynamic_cast<QGraphicsTextItem*>(item))label->setPlainText(text->text());Remember();});
    auto* front=new QPushButton(QStringLiteral("選択を前へ"),panel);layout->addWidget(front);
    connect(front,&QPushButton::clicked,this,[this]{before_=Snapshot();double z=0;for(auto* item:scenes_[current_]->items())z=std::max(z,item->zValue());
        for(auto* item:scenes_[current_]->selectedItems())item->setZValue(++z);Remember();});
    auto* help=new QLabel(QStringLiteral("部品・矢印・文字をドラッグで移動。複数選択可。Deleteで説明図から除去、Ctrl+Zで戻す。元のCADは変更しません。\n説明書は専用 .kci ファイルに保存。CADの保存とは別です。"),panel);
    help->setWordWrap(true);layout->addWidget(help);layout->addStretch();
    window_.OperationHost().ShowTemporaryPage(panel,QStringLiteral("組み立て説明書"));
}
void V2InstructionMode::Command(std::string_view id){
    if(!settings_||!settings_->isVisible())ShowSettings();
    if(id=="instructions.open"||id=="instructions.save"||id=="instructions.pdf"){FileAction(id);return;}
    if(id=="instructions.new_page"){NewPage(false);return;}
    if(id=="instructions.duplicate"){NewPage(true);return;}
    if(id=="instructions.capture"){Capture();return;}
    CancelArrow();arrowTool_=id=="instructions.arrow";
    if(hint_)hint_->setText(QStringLiteral("説明図をドラッグで配置。文字は選択して右の欄で更新。元のCADは変更しません。"));
    if(arrowTool_){if(hint_)hint_->setText(QStringLiteral("矢印の始点 → 終点をページ上でクリック。Escで中止。"));return;}
    before_=Snapshot();
    if(id=="instructions.text"){
        auto* input=settings_?settings_->findChild<QLineEdit*>(QStringLiteral("instructionText")):nullptr;
        auto* item=scenes_[current_]->addText(input&&!input->text().isEmpty()?input->text():QStringLiteral("1"),QFont(QStringLiteral("Meiryo"),20));
        item->setData(0,"text");item->setPos(70,70);item->setDefaultTextColor(Qt::black);
        item->setFlags(QGraphicsItem::ItemIsMovable|QGraphicsItem::ItemIsSelectable);item->setZValue(1000);
    }else if(id=="instructions.remove")for(auto* item:scenes_[current_]->selectedItems())delete item;
    Remember();
}
void V2InstructionMode::Remember(){
    if(loading_||before_==Snapshot())return;
    undo_.push_back(before_);if(undo_.size()>30)undo_.erase(undo_.begin());redo_.clear();dirty_=true;
}
void V2InstructionMode::Undo(bool redo){
    CancelArrow();auto& source=redo?redo_:undo_;auto& target=redo?undo_:redo_;if(source.empty())return;
    target.push_back(Snapshot());const auto saved=source.back();source.pop_back();Restore(saved);dirty_=true;
}
void V2InstructionMode::CancelArrow(){if(arrow_){delete arrow_;arrow_=nullptr;}}
void V2InstructionMode::DrawArrow(const QPointF& end){
    if(!arrow_)return;const auto delta=end-arrowStart_;const double length=std::hypot(delta.x(),delta.y());
    QPainterPath path;path.moveTo(arrowStart_);path.lineTo(end);
    if(length>1){const auto d=delta/length;const QPointF side(-d.y(),d.x());path.moveTo(end-d*18+side*8);path.lineTo(end);path.lineTo(end-d*18-side*8);}
    arrow_->setPath(path);arrow_->setData(1,QVariant::fromValue(QJsonArray{arrowStart_.x(),arrowStart_.y(),end.x(),end.y()}));
}
bool V2InstructionMode::eventFilter(QObject* object,QEvent* event){
    if(object==&window_&&event->type()==QEvent::Close&&!CheckSaved()){static_cast<QCloseEvent*>(event)->ignore();return true;}
    if(!isVisible())return false;
    if(object==view_->viewport()){
        if(event->type()==QEvent::MouseButtonPress){auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)return false;
            before_=Snapshot();if(!arrowTool_)return false;const auto at=view_->mapToScene(mouse->position().toPoint());
            if(!view_->sceneRect().contains(at))return true;
            if(!arrow_){arrowStart_=at;arrow_=scenes_[current_]->addPath({},QPen(QColor(190,40,20),3));arrow_->setZValue(999);}
            else {DrawArrow(at);if(QLineF(arrowStart_,at).length()<2)return true;arrow_->setData(0,"arrow");
                arrow_->setFlags(QGraphicsItem::ItemIsMovable|QGraphicsItem::ItemIsSelectable);arrow_=nullptr;Remember();}return true;
        }
        if(event->type()==QEvent::MouseMove&&arrow_){DrawArrow(view_->mapToScene(static_cast<QMouseEvent*>(event)->position().toPoint()));return true;}
        if(event->type()==QEvent::MouseButtonRelease&&!arrowTool_)Remember();
    }
    if(event->type()==QEvent::KeyPress){auto* widget=qobject_cast<QWidget*>(object);if(!widget||widget->window()!=window())return false;
        if(qobject_cast<QLineEdit*>(object))return false;const auto* key=static_cast<QKeyEvent*>(event);
        if((key->modifiers()&Qt::ControlModifier)&&(key->key()==Qt::Key_Z||key->key()==Qt::Key_Y)){
            Undo(key->key()==Qt::Key_Y||(key->modifiers()&Qt::ShiftModifier));return true;}
        if(key->key()==Qt::Key_Escape){CancelArrow();arrowTool_=false;return true;}
        if(key->key()==Qt::Key_Delete){Command("instructions.remove");return true;}
    }
    return false;
}
void V2InstructionMode::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);if(view_->scene())view_->fitInView(view_->sceneRect(),Qt::KeepAspectRatio);}
bool V2InstructionMode::CheckSaved(){
    if(!dirty_)return true;
    const auto answer=QMessageBox::question(&window_,QStringLiteral("説明書の保存"),QStringLiteral("説明書の変更を保存しますか？"),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel);
    if(answer==QMessageBox::Cancel)return false;if(answer==QMessageBox::Discard)return true;
    FileAction("instructions.save");return !dirty_;
}
