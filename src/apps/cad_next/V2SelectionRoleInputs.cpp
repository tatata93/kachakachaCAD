#include "V2SelectionRoleInputs.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2RoleInput.h"
#include "V2ToolSelection.h"
#include <QDockWidget>
#include <QLabel>
#include <QObject>
#include <QComboBox>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QLayout>
#include <QWidget>
#include <QScrollArea>
#include <QShowEvent>
#include <QHideEvent>
#include <QSignalBlocker>
#include <QApplication>
#include <QEvent>
#include <QTimer>
#include <algorithm>
#include <array>

namespace {
using namespace kachakacha::v2;
class SelectionRoles final : public QWidget {
public:
    SelectionRoles(V2MainWindow& window, QWidget* body, int kind)
        : QWidget(body), window_(window), corner_(kind==1), kind_(kind) {
        setObjectName(kind==1?"cornerSelectionRoles":kind==2?"transformSelectionRoles":kind==3?"approxSelectionRoles":"arraySelectionRoles");
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSizeConstraint(QLayout::SetMinimumSize);
        for(int i=0;i<(corner_?3:1);++i) {
            auto* role=new V2RoleInput(this,corner_?(i==0?QStringLiteral("線 A"):i==1?QStringLiteral("線 B"):QStringLiteral("一括加工の追加辺")):kind==2?QStringLiteral("移動・複写する対象"):kind==3?QStringLiteral("近似する面・立体"):QStringLiteral("並べる対象"));
            roles_[i]=role;layout->addWidget(role);
            role->arm->setObjectName(QStringLiteral("selectionRolePick%1").arg(i));
            role->clear->setObjectName(QStringLiteral("selectionRoleClear%1").arg(i));
            connect(role->arm,&QPushButton::clicked,this,[this,i]{Arm(i);});
            connect(role->clear,&QPushButton::clicked,this,[this,i]{Cancel();inputs_[i]={};Publish();});
            role->removeItem=[this,i](int row){
                if(row<0||row>=static_cast<int>(inputs_[i].ordered.size()))return;
                Cancel();inputs_[i].ordered.erase(inputs_[i].ordered.begin()+row);Publish();
            };
        }
        if(kind_==2){
            done_=new QPushButton(QStringLiteral("選択終了 → 基準点を指定"),this);
            done_->setObjectName("transformTargetsDone");layout->addWidget(done_);done_->hide();
            connect(done_,&QPushButton::clicked,this,[this]{Cancel();});
            roles_[0]->arm->setToolTip(QStringLiteral("複数の線・部品をクリックや範囲で追加します。Ctrlでも追加・解除できます。"));
            base_=new QLabel(this);base_->setObjectName("transformBasePoint");base_->setWordWrap(true);layout->addWidget(base_);
            destination_=new QLabel(this);destination_->setObjectName("transformDestination");destination_->setWordWrap(true);layout->addWidget(destination_);
            resetBase_=new QPushButton(QStringLiteral("基準点を取り直す"),this);resetBase_->setObjectName("transformResetBase");layout->addWidget(resetBase_);
            connect(resetBase_,&QPushButton::clicked,this,[this]{Cancel();window_.Viewport().StepBackInputPoint();Refresh();window_.Viewport().setFocus();});
            qApp->installEventFilter(this);
        }
    }
    void Sync() {
        if(!isVisible()||writing_||V2ToolSelection::Active(window_.Viewport()))return;
        if(kind_==2&&!window_.Viewport().property("transformTargetPicking").toBool())active_=-1;
        auto selection=window_.Viewport().Selection();
        if(selection.ordered.empty())for(const auto& id:selection.entityIds){app::SelectionRef ref;ref.entityId=id;selection.ordered.push_back(ref);}
        const bool polyline = PolylineMode();
        if (polyline != lastPolyline_) { last_.clear(); lastPolyline_ = polyline; }
        if(selection.ordered.size()==last_.size()&&std::equal(selection.ordered.begin(),selection.ordered.end(),last_.begin(),
            [](const auto& a,const auto& b){return a.entityId==b.entityId&&a.segmentId==b.segmentId;})){Refresh();return;}
        last_=selection.ordered;inputs_={};
        int index=0;
        for(const auto& ref:selection.ordered) {
            if(!Accept(ref.entityId))continue;
            inputs_[corner_&&!polyline?std::min(index++,2):0].ordered.push_back(ref);
        }
        Refresh();
    }
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if(kind_==2 && isVisible() && ((object==&window_.Viewport()&&event->type()==QEvent::MouseButtonRelease)
            || event->type()==QEvent::KeyRelease))QTimer::singleShot(0,this,[this]{Sync();});
        return false;
    }
    void showEvent(QShowEvent* event) override {QWidget::showEvent(event);Sync();}
    void hideEvent(QHideEvent* event) override {Cancel();QWidget::hideEvent(event);}
private:
    bool PolylineMode() const {
        const auto* mode = window_.findChild<QComboBox*>(QStringLiteral("cornerTargetMode"));
        return corner_ && mode && mode->currentIndex() == 1;
    }
    bool Accept(base::EntityId id) const {
        const auto* entity=window_.Session().GetDocument().FindEntity(id);
        return entity&&((kind_==3?entity->kind==domain::EntityKind::GuideSurface:entity->kind==domain::EntityKind::Wire)||(!corner_&&entity->kind==domain::EntityKind::Part));
    }
    void Cancel() {
        if(kind_==2)window_.Viewport().setProperty("transformTargetPicking",false);
        else if(active_>=0)window_.Viewport().CancelReferencePick();
        active_=-1;Refresh();
    }
    void Arm(int index) {
        if(kind_==2){const bool active=active_==index;Cancel();if(!active){active_=index;window_.Viewport().setProperty("transformTargetPicking",true);}Refresh();return;}
        Cancel();active_=index;Refresh();
        window_.Viewport().BeginElementPick([this](const app::PickCandidate& pick){return Accept(pick.entityId);},
            [this,index](std::optional<app::PickCandidate> pick){
                active_=-1;
                if(pick) {
                    inputs_[index]=app::ApplySelection(corner_&&!PolylineMode()&&index<2?app::SelectionSet{}:inputs_[index],pick,app::SelectionMode::Add);
                    Publish();
                } else Refresh();
            });
    }
    void Publish() {
        app::SelectionSet selection;
        for(auto& input:inputs_) {
            input.entityIds.clear();
            for(const auto& ref:input.ordered) {
                input.entityIds.push_back(ref.entityId);selection.ordered.push_back(ref);
                if(std::find(selection.entityIds.begin(),selection.entityIds.end(),ref.entityId)==selection.entityIds.end())selection.entityIds.push_back(ref.entityId);
            }
        }
        last_=selection.ordered;writing_=true;window_.Viewport().SetSelection(std::move(selection));writing_=false;Refresh();
    }
    void Refresh() {
        const bool polyline = PolylineMode();
        if(done_)done_->setVisible(active_>=0);
        if(base_){const auto tool=window_.Session().CurrentTool();const bool translating=tool==modeling::DrawingTool::Move||tool==modeling::DrawingTool::Copy;
            base_->setVisible(translating);destination_->setVisible(translating);resetBase_->setVisible(translating);
            const bool placed=window_.Session().HasPlacedPoints();const auto p=window_.Session().ConstraintAnchor();
            base_->setText(placed?QStringLiteral("② 基準点：X %1 / Y %2 / Z %3 mm").arg(p.x,0,'f',3).arg(p.y,0,'f',3).arg(p.z,0,'f',3)
                :QStringLiteral("② 基準点：対象を選び終えたら3Dでクリック"));
            destination_->setText(placed?QStringLiteral("③ 移動先：3Dでクリック。下見がカーソルに追従します。")
                :QStringLiteral("③ 移動先：基準点を決めた後に指定"));resetBase_->setEnabled(placed);
        }
        if (corner_) roles_[0]->name->setText(polyline ? QStringLiteral("加工するワイヤー全体") : QStringLiteral("線 A"));
        for(int i=0;i<(corner_?3:1);++i) {
            roles_[i]->setVisible(!polyline || i == 0);
            QStringList names;
            for(const auto& ref:inputs_[i].ordered)if(const auto* entity=window_.Session().GetDocument().FindEntity(ref.entityId))names.push_back(QString::fromStdString(entity->displayName));
            roles_[i]->value->setText(names.empty()?QStringLiteral("未選択"):names.join(QStringLiteral("、")));
            roles_[i]->SetItems(names);roles_[i]->arm->setChecked(active_==i);
        }
    }
    V2MainWindow& window_;bool corner_=false,writing_=false,lastPolyline_=false;int kind_=0,active_=-1;
    QPushButton* done_=nullptr;
    QLabel* base_=nullptr;QLabel* destination_=nullptr;QPushButton* resetBase_=nullptr;
    std::vector<app::SelectionRef> last_;
    std::array<V2RoleInput*,3> roles_{};std::array<app::SelectionSet,3> inputs_{};
};
}
void InstallSelectionRoleInputs(V2MainWindow& window,QDockWidget& dock,int kind) {
    QWidget* body=dock.widget();
    if(auto* scroll=body->findChild<QScrollArea*>())body=scroll->widget();
    if(auto* layout=qobject_cast<QVBoxLayout*>(body->layout()))layout->insertWidget(0,new SelectionRoles(window,body,kind));
}
void RefreshSelectionRoleInputs(V2MainWindow& window) {
    for(auto* widget:window.findChildren<QWidget*>())if(auto* role=dynamic_cast<SelectionRoles*>(widget))role->Sync();
}
