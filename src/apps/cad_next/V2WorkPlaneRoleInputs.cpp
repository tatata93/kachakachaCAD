#include "V2WorkPlaneRoleInputs.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2WorkPlaneDock.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QString>
#include <QWidget>
#include <QVariant>
#include <QObject>
#include <QPushButton>
#include <array>
#include <algorithm>
#include <optional>

namespace {
using namespace kachakacha::v2;
class RoleInputs final : public QWidget {
public:
    RoleInputs(V2MainWindow& window, V2WorkPlaneDock& dock, std::function<void()> changed)
        : QWidget(dock.widget()), window_(window), dock_(dock), changed_(std::move(changed)),
          document_(window.Session().GetDocument().Snapshot().id) {
        setObjectName(QStringLiteral("workPlaneRoleInputs"));
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        for(int i=0;i<7;++i) {
            rows_[i]=new QWidget(this);auto* row=new QHBoxLayout(rows_[i]);row->setContentsMargins(0,0,0,0);
            pick_[i]=new QPushButton(rows_[i]);pick_[i]->setCheckable(true);
            pick_[i]->setObjectName(QStringLiteral("workPlanePickRole%1").arg(i));
            values_[i]=new QLabel(rows_[i]);values_[i]->setWordWrap(true);
            auto* clear=new QPushButton(QStringLiteral("解除"),rows_[i]);
            clear->setObjectName(QStringLiteral("workPlaneClearRole%1").arg(i));
            row->addWidget(pick_[i]);row->addWidget(values_[i],1);row->addWidget(clear);layout->addWidget(rows_[i]);
            connect(pick_[i],&QPushButton::clicked,this,[this,i]{Arm(i);});
            connect(clear,&QPushButton::clicked,this,[this,i]{Clear(i);});
        }
        Sync();
    }
    void Sync() {
        if(document_!=window_.Session().GetDocument().Snapshot().id) {
            document_=window_.Session().GetDocument().Snapshot().id;
            points_={};edges_={};explicitPoints_=explicitEdges_=explicitPlanes_=false;active_=-1;
        }
        const auto choice=dock_.Choice();const auto needs=app::NeedsOf(choice.method);
        for(int i=0;i<7;++i) {
            const bool point=i<3,edge=i>=3&&i<5;
            rows_[i]->setVisible(point?i<needs.points:edge?i-3<needs.edges:i-5<needs.planes);
            const QString title=point?QStringLiteral("点%1").arg(i+1):edge?QStringLiteral("線%1").arg(i-2):i==5?QStringLiteral("基準面"):QStringLiteral("相手面");
            pick_[i]->setText(title+(active_==i?QStringLiteral(" 選択中"):QStringLiteral("を選ぶ")));
            pick_[i]->setChecked(active_==i);
            QString value=QStringLiteral("未選択");
            if(point) {
                const auto p=choice.method==modeling::WorkPlaneMethod::ThreePoints
                    ?std::optional<geometry::Vector3>{choice.threePoints[i]}:points_[i];
                if(p)value=QStringLiteral("%1, %2, %3 mm").arg(p->x,0,'g',5).arg(p->y,0,'g',5).arg(p->z,0,'g',5);
            } else if(edge && edges_[i-3]) value=QStringLiteral("指定済み");
            else if(!edge) {
                const auto id=i==5?choice.referencePlaneId:choice.secondPlaneId;
                if(id)if(const auto* entity=window_.Session().GetDocument().FindEntity(*id))value=QString::fromStdString(entity->displayName);
            }
            values_[i]->setText(value);
        }
    }
    void Apply(const app::WorkPlaneChoice& choice,app::WorkPlaneMaterials& result) const {
        if(!isVisible() || document_!=window_.Session().GetDocument().Snapshot().id)return;
        if(explicitPoints_) {
            result.points.clear();
            if(choice.method!=modeling::WorkPlaneMethod::ThreePoints)
                for(const auto& point:points_) {if(!point)break;result.points.push_back(*point);}
        }
        if(explicitEdges_) {
            result.edges.clear();
            for(const auto& edge:edges_){if(!edge)break;result.edges.push_back(*edge);}
        }
        if(explicitPlanes_)result.selectedPlanes.clear();
    }
private:
    void Changed() {active_=-1;Sync();if(changed_)changed_();}
    void CancelPick() {window_.Viewport().CancelPointPick();window_.Viewport().CancelReferencePick();}
    void Seed() {
        const auto input=window_.CollectWorkPlaneMaterials(dock_.Choice());
        if(!explicitPoints_)for(std::size_t i=0;i<input.points.size()&&i<3;++i)points_[i]=input.points[i];
        if(!explicitEdges_)for(std::size_t i=0;i<input.edges.size()&&i<2;++i)edges_[i]=input.edges[i];
    }
    void Arm(int role) {
        CancelPick();Seed();active_=role;Sync();
        if(role<3) {
            window_.Viewport().BeginPointPick([this,role](const V2Viewport::PickedPoint& picked){
                explicitPoints_=true;points_[role]=picked.point;
                auto choice=dock_.Choice();choice.threePoints[role]=picked.point;dock_.SetChoice(choice);Changed();
            },"作業面: 指定する点をクリックしてください。他の入力は保持します。");
            return;
        }
        window_.Viewport().BeginElementPick([this,role](const app::PickCandidate& pick){
            if(role>=5)return window_.WorkPlaneFrameOf(pick.entityId).has_value();
            if(!pick.segmentId.IsNil())return true;
            return std::count_if(window_.Session().Scene().curves.begin(),window_.Session().Scene().curves.end(),
                [&](const auto& curve){return curve.entityId==pick.entityId;})==1;
        },[this,role](std::optional<app::PickCandidate> pick){
            if(!pick){Changed();return;}
            if(role>=5) {
                explicitPlanes_=true;auto choice=dock_.Choice();
                (role==5?choice.referencePlaneId:choice.secondPlaneId)=pick->entityId;dock_.SetChoice(choice);
            } else for(const auto& curve:window_.Session().Scene().curves) {
                if(curve.entityId!=pick->entityId||(!pick->segmentId.IsNil()&&curve.segmentId!=pick->segmentId))continue;
                explicitEdges_=true;edges_[role-3]=curve.segment;break;
            }
            Changed();
        });
    }
    void Clear(int role) {
        CancelPick();Seed();
        if(role<3){explicitPoints_=true;points_[role].reset();}
        else if(role<5){explicitEdges_=true;edges_[role-3].reset();}
        else {explicitPlanes_=true;auto choice=dock_.Choice();
            (role==5?choice.referencePlaneId:choice.secondPlaneId).reset();dock_.SetChoice(choice);}
        Changed();
    }
    V2MainWindow& window_;V2WorkPlaneDock& dock_;std::function<void()> changed_;
    base::DocumentId document_;
    std::array<std::optional<geometry::Vector3>,3> points_;
    std::array<std::optional<geometry::CurveSegment>,2> edges_;
    std::array<QWidget*,7> rows_{};std::array<QPushButton*,7> pick_{};std::array<QLabel*,7> values_{};
    bool explicitPoints_=false,explicitEdges_=false,explicitPlanes_=false;int active_=-1;
};
RoleInputs* Find(const V2WorkPlaneDock* dock) {
    return dock?dynamic_cast<RoleInputs*>(dock->property("workPlaneRoleInputs").value<QObject*>()):nullptr;
}
}
void InstallWorkPlaneRoleInputs(V2MainWindow& window,V2WorkPlaneDock& dock,std::function<void()> changed) {
    auto* role=new RoleInputs(window,dock,std::move(changed));
    dock.setProperty("workPlaneRoleInputs",QVariant::fromValue(static_cast<QObject*>(role)));
    if(auto* layout=qobject_cast<QVBoxLayout*>(dock.widget()->layout()))layout->insertWidget(1,role);
}
void ApplyWorkPlaneRoleInputs(const V2WorkPlaneDock* dock,const kachakacha::v2::app::WorkPlaneChoice& choice,
    kachakacha::v2::app::WorkPlaneMaterials& materials) {if(const auto* role=Find(dock))role->Apply(choice,materials);}
void SyncWorkPlaneRoleInputs(V2WorkPlaneDock& dock) {if(auto* role=Find(&dock))role->Sync();}
