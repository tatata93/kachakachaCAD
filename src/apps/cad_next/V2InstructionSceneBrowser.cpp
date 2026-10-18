#include "V2InstructionSceneBrowser.h"
#include <QAbstractItemView>
#include <QIcon>
#include <QLabel>
#include <QEvent>
#include <QListView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QObject>
#include <QPushButton>
#include <QPixmap>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QString>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

V2InstructionSceneBrowser::V2InstructionSceneBrowser(QWidget* parent):QWidget(parent)
{
    setObjectName("instructionSceneBrowser");setMinimumWidth(170);
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(4,4,4,4);
    layout->addWidget(new QLabel(QStringLiteral("作ったシーン"),this));
    auto* help=new QLabel(QStringLiteral("用紙編集中はシーンを選び、用紙をクリックして配置。ダブルクリックで編集。"),this);
    help->setWordWrap(true);layout->addWidget(help);
    list_=new QListWidget(this);list_->setObjectName("instructionSceneList");
    list_->setViewMode(QListView::IconMode);list_->setFlow(QListView::TopToBottom);
    list_->setWrapping(false);list_->setMovement(QListView::Static);list_->setResizeMode(QListView::Adjust);
    list_->setIconSize({144,96});list_->setGridSize({160,128});list_->setWordWrap(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(list_,1);
    const auto button=[this,layout](const char* text,const char* name){auto* b=new QPushButton(QString::fromUtf8(text),this);b->setObjectName(name);layout->addWidget(b);return b;};
    add_=button("新しいシーン","instructionBrowserAdd");
    edit_=button("シーンを編集","instructionBrowserEdit");
    place_=button("用紙へ配置","instructionBrowserPlace");
    remove_=button("シーンを削除","instructionBrowserRemove");
    connect(add_,&QPushButton::clicked,this,[this]{if(addScene)addScene();});
    connect(edit_,&QPushButton::clicked,this,[this]{if(editScene)editScene(SelectedScene());});
    connect(place_,&QPushButton::clicked,this,[this]{if(placeScene)placeScene(SelectedScene());});
    connect(remove_,&QPushButton::clicked,this,[this]{if(removeScene)removeScene();});
    connect(list_,&QListWidget::currentRowChanged,this,[this]{if(chooseScene)chooseScene(SelectedScene());});
    connect(list_,&QListWidget::itemClicked,this,[this]{if(chooseScene)chooseScene(SelectedScene());});
    connect(list_,&QListWidget::itemDoubleClicked,this,[this]{if(editScene)editScene(SelectedScene());});
    timer_=new QTimer(this);timer_->setSingleShot(true);timer_->setInterval(100);
    connect(timer_,&QTimer::timeout,this,[this]{UpdateThumbnails();});
    connect(list_->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{if(!timer_->isActive())timer_->start();});
    list_->viewport()->installEventFilter(this);
}
bool V2InstructionSceneBrowser::eventFilter(QObject* object,QEvent* event)
{
    if(object==list_->viewport()&&(event->type()==QEvent::Resize||event->type()==QEvent::Show))
        if(!timer_->isActive())timer_->start();
    return QWidget::eventFilter(object,event);
}
int V2InstructionSceneBrowser::SelectedScene() const
{
    const auto* item=list_->currentItem();if(!item||!scenes_)return -1;
    const auto id=item->data(Qt::UserRole).toUuid();
    for(std::size_t i=0;i<scenes_->size();++i)if((*scenes_)[i].id==id)return int(i);
    return -1;
}
void V2InstructionSceneBrowser::Refresh(std::vector<InstructionPage>* scenes,int selected,bool paper,bool choosing)
{
    scenes_=scenes;const QSignalBlocker blocked(list_);
    const int count=scenes?int(scenes->size()):0;
    while(list_->count()>count)delete list_->takeItem(list_->count()-1);
    while(list_->count()<count)list_->addItem(new QListWidgetItem);
    for(int i=0;i<count;++i){const auto& scene=(*scenes)[i];auto* item=list_->item(i);
        item->setData(Qt::UserRole,scene.id);item->setText(QStringLiteral("%1  %2").arg(i+1).arg(scene.title));
        item->setToolTip(scene.title+QStringLiteral("\n")+(paper?QStringLiteral("選んで用紙をクリックすると配置"):QStringLiteral("選んで3Dシーンを編集")));
        item->setIcon(icons_.value(scene.id));
    }
    list_->setCurrentRow(selected>=0&&selected<count?selected:-1);
    list_->setEnabled(!choosing);add_->setEnabled(!choosing);
    edit_->setEnabled(!choosing&&selected>=0&&selected<count);place_->setEnabled(edit_->isEnabled());
    remove_->setEnabled(edit_->isEnabled());
    if(!timer_->isActive())timer_->start();
}
void V2InstructionSceneBrowser::Invalidate(const QUuid& id)
{
    if(id.isNull())icons_.clear();else icons_.remove(id);
}
void V2InstructionSceneBrowser::UpdateThumbnails()
{
    if(!isVisible()||!scenes_)return;
    for(int i=0;i<list_->count();++i){auto* item=list_->item(i);const auto id=item->data(Qt::UserRole).toUuid();
        if(icons_.contains(id)||!list_->visualItemRect(item).intersects(list_->viewport()->rect()))continue;
        const auto found=std::find_if(scenes_->begin(),scenes_->end(),[id](const auto& scene){return scene.id==id;});
        if(found==scenes_->end())continue;
        V2InstructionScene renderer(nullptr);renderer.SetPage(&*found);
        const QIcon icon(QPixmap::fromImage(renderer.Render({144,96})));icons_.insert(id,icon);item->setIcon(icon);
    }
    // Deleted scene previews must not grow the cache through repeated add/delete operations.
    for(auto it=icons_.begin();it!=icons_.end();)if(std::none_of(scenes_->begin(),scenes_->end(),[&](const auto& scene){return scene.id==it.key();}))it=icons_.erase(it);else ++it;
}
