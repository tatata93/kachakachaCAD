#include "V2SelectionRoleInputs.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2RoleInput.h"
#include <QDockWidget>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>
#include <QScrollArea>
#include <QShowEvent>
#include <QHideEvent>
#include <QSignalBlocker>
#include <algorithm>
#include <array>

namespace {
using namespace kachakacha::v2;
class SelectionRoles final : public QWidget {
public:
    SelectionRoles(V2MainWindow& window, QWidget* body, int kind)
        : QWidget(body), window_(window), corner_(kind==1), kind_(kind) {
        setObjectName(kind==1?"cornerSelectionRoles":kind==2?"transformSelectionRoles":kind==3?"approxSelectionRoles":"arraySelectionRoles");
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
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
    }
    void Sync() {
        if(!isVisible()||writing_)return;
        auto selection=window_.Viewport().Selection();
        if(selection.ordered.empty())for(const auto& id:selection.entityIds){app::SelectionRef ref;ref.entityId=id;selection.ordered.push_back(ref);}
        if(selection.ordered.size()==last_.size()&&std::equal(selection.ordered.begin(),selection.ordered.end(),last_.begin(),
            [](const auto& a,const auto& b){return a.entityId==b.entityId&&a.segmentId==b.segmentId;})){Refresh();return;}
        last_=selection.ordered;inputs_={};
        int index=0;
        for(const auto& ref:selection.ordered) {
            if(!Accept(ref.entityId))continue;
            inputs_[corner_?std::min(index++,2):0].ordered.push_back(ref);
        }
        Refresh();
    }
protected:
    void showEvent(QShowEvent* event) override {QWidget::showEvent(event);Sync();}
    void hideEvent(QHideEvent* event) override {Cancel();QWidget::hideEvent(event);}
private:
    bool Accept(base::EntityId id) const {
        const auto* entity=window_.Session().GetDocument().FindEntity(id);
        return entity&&((kind_==3?entity->kind==domain::EntityKind::GuideSurface:entity->kind==domain::EntityKind::Wire)||(!corner_&&entity->kind==domain::EntityKind::Part));
    }
    void Cancel() {if(active_>=0)window_.Viewport().CancelReferencePick();active_=-1;Refresh();}
    void Arm(int index) {
        Cancel();active_=index;Refresh();
        window_.Viewport().BeginElementPick([this](const app::PickCandidate& pick){return Accept(pick.entityId);},
            [this,index](std::optional<app::PickCandidate> pick){
                active_=-1;
                if(pick) {
                    inputs_[index]=app::ApplySelection(corner_&&index<2?app::SelectionSet{}:inputs_[index],pick,app::SelectionMode::Add);
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
        for(int i=0;i<(corner_?3:1);++i) {
            QStringList names;
            for(const auto& ref:inputs_[i].ordered)if(const auto* entity=window_.Session().GetDocument().FindEntity(ref.entityId))names.push_back(QString::fromStdString(entity->displayName));
            roles_[i]->value->setText(names.empty()?QStringLiteral("未選択"):names.join(QStringLiteral("、")));
            roles_[i]->SetItems(names);roles_[i]->arm->setChecked(active_==i);
        }
    }
    V2MainWindow& window_;bool corner_=false,writing_=false;int kind_=0,active_=-1;
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
