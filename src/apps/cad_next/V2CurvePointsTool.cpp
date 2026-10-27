#include "V2CurvePointsTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2RoleInput.h"
#include "V2NumericSpinBox.h"
#include "V2OperationPanelHost.h"
#include "V2EntityTree.h"
#include "V2ToolSelection.h"
#include "V2PanelFrame.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Commands.h"
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QSpinBox>
#include <QAbstractSpinBox>
#include <QScrollArea>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cmath>
using namespace kachakacha::v2;
namespace {
constexpr std::size_t limit=1000;
constexpr double accuracy=1e-6;
// One candidate validation and history snapshot for the whole placement. Running
// one document command per point copied the growing document up to 1,000 times.
class AddCurvePoints final : public document::DocumentCommand {
public:
    std::vector<document::AddFeatureCommand> commands;
    std::string Label() const override {return "曲線に等間隔の点";}
    std::vector<base::Diagnostic> Apply(document::DocumentSnapshot& candidate) const override {
        for(const auto& command:commands){auto diagnostics=command.Apply(candidate);
            if(!diagnostics.empty())return diagnostics;}
        return {};
    }
};
// Cache short arc-length intervals; solving within one interval avoids repeatedly
// integrating the entire spline for every preview point.
class ArcPositions {
public:
    explicit ArcPositions(const geometry::CurveSegment& curve):curve_(curve) {
        for(std::size_t i=1;i<cumulative_.size();++i)
            cumulative_[i]=cumulative_[i-1]+curve.ArcLength(double(i-1)/64,double(i)/64,accuracy/128);
    }
    double Length() const {return cumulative_.back();}
    geometry::Vector3 At(double distance) const {
        if(distance<=0)return curve_.StartPoint();
        if(distance>=Length())return curve_.EndPoint();
        const auto found=std::upper_bound(cumulative_.begin(),cumulative_.end(),distance);
        const auto index=std::size_t(found-cumulative_.begin()-1);
        const double begin=double(index)/64,target=distance-cumulative_[index];
        double low=begin,high=double(index+1)/64;
        for(int i=0;i<32;++i){const double middle=(low+high)*0.5;
            if(curve_.ArcLength(begin,middle,accuracy/128)<target)low=middle;else high=middle;}
        return curve_.Evaluate((low+high)*0.5);
    }
private:
    const geometry::CurveSegment& curve_;
    std::array<double,65> cumulative_{};
};
}
void V2CurvePointsTool::Open(V2MainWindow& window) {
    if(dynamic_cast<V2CurvePointsTool*>(window.OperationHost().TemporaryPage()))return;
    const auto selection=window.Viewport().Selection();
    window.SelectTool(modeling::DrawingTool::Select);window.Viewport().SetSelection(selection);
    auto* tool=new V2CurvePointsTool(window);
    window.OperationHost().ShowTemporaryPage(tool,QStringLiteral("曲線に等間隔の点"));
    window.Viewport().SetToolPickActive(true);window.Viewport().SetToolPickToggle(true,true);
    window.Viewport().SetToolCandidateFilter([&window](const app::PickCandidate& p){
        const auto* e=window.Session().GetDocument().FindEntity(p.entityId);return e&&e->kind==domain::EntityKind::Wire;});
    tool->Refresh();
}
V2CurvePointsTool::V2CurvePointsTool(V2MainWindow& window):QWidget(&window),window_(window) {
    setObjectName("curvePointsPanel");auto* outer=new QVBoxLayout(this);
    auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    auto* body=new QWidget(scroll);auto* layout=new QVBoxLayout(body);scroll->setWidget(body);outer->addWidget(scroll,1);
    auto* hint=new QLabel(QStringLiteral("手摺脚・リベット・支柱の位置決め。各辺の曲線に沿った長さで点を並べます。線は加工しません。"),this);
    hint->setWordWrap(true);layout->addWidget(hint);
    role_=new V2RoleInput(this,QStringLiteral("① 点を並べる辺（複数可）"));role_->setObjectName("curvePointsTargets");role_->arm->setChecked(true);layout->addWidget(role_);
    role_->arm->setObjectName("curvePointsPick");role_->clear->setObjectName("curvePointsClear");
    connect(role_->clear,&QPushButton::clicked,this,[this]{window_.Viewport().SetSelection({});Refresh();});
    connect(role_->arm,&QPushButton::clicked,this,[this]{role_->arm->setChecked(true);window_.Viewport().setFocus();});
    role_->removeItem=[this](int row){app::SelectionSet selection;
        if(row<0||row>=int(inputs_.size()))return;
        inputs_.erase(inputs_.begin()+row);selection.ordered=inputs_;
        for(const auto& ref:inputs_)if(std::find(selection.entityIds.begin(),selection.entityIds.end(),ref.entityId)==selection.entityIds.end())selection.entityIds.push_back(ref.entityId);
        window_.Viewport().SetSelection(selection);Refresh();};
    layout->addWidget(new QLabel(QStringLiteral("② 並べ方"),this));
    mode_=new QComboBox(this);mode_->setObjectName("curvePointsMode");mode_->addItems({QStringLiteral("間隔を指定"),QStringLiteral("等分する数を指定")});layout->addWidget(mode_);
    pitch_=new V2NumericSpinBox(this);pitch_->setObjectName("curvePointsPitch");pitch_->setRange(0.001,100000);pitch_->setDecimals(3);pitch_->setSuffix(QStringLiteral(" mm 間隔"));pitch_->setValue(2);layout->addWidget(pitch_);
    divisions_=new QSpinBox(this);divisions_->setObjectName("curvePointsDivisions");divisions_->setRange(1,999);divisions_->setSuffix(QStringLiteral(" 等分"));divisions_->setValue(8);layout->addWidget(divisions_);
    endpoints_=new QCheckBox(QStringLiteral("端点も作る（最後の間隔は短くなる場合があります）"),this);endpoints_->setObjectName("curvePointsEndpoints");endpoints_->setChecked(true);layout->addWidget(endpoints_);
    reverse_=new QCheckBox(QStringLiteral("点を並べ始める向きを反転"),this);reverse_->setObjectName("curvePointsReverse");layout->addWidget(reverse_);
    status_=new QLabel(this);status_->setObjectName("curvePointsStatus");status_->setWordWrap(true);layout->addWidget(status_);
    auto* note=new QLabel(QStringLiteral("各辺ごとに指定します。複数の辺をまたぐ間隔にはしません。確定した点は座標固定で、元の線には追従しません。上限は合計1,000点です。"),this);note->setWordWrap(true);layout->addWidget(note);layout->addStretch();
    auto* row=new QHBoxLayout;auto* cancel=new QPushButton(this);confirm_=new QPushButton(this);MarkCancelConfirm(cancel,confirm_);
    confirm_->setObjectName("curvePointsConfirm");row->addWidget(cancel);row->addWidget(confirm_);outer->addLayout(row);
    connect(cancel,&QPushButton::clicked,this,[this]{window_.Viewport().SetSelection({});Refresh();});
    connect(confirm_,&QPushButton::clicked,this,[this]{Confirm();});
    timer_=new QTimer(this);timer_->setSingleShot(true);timer_->setInterval(100);connect(timer_,&QTimer::timeout,this,[this]{Refresh();});
    connect(window.EntityTree(),&QTreeWidget::itemSelectionChanged,this,[this]{if(isVisible())timer_->start(0);});
    connect(mode_,&QComboBox::currentIndexChanged,this,[this]{Refresh();});
    connect(pitch_,&QDoubleSpinBox::valueChanged,this,[this]{QueueRefresh();});
    connect(divisions_,&QSpinBox::valueChanged,this,[this]{QueueRefresh();});
    connect(endpoints_,&QCheckBox::toggled,this,[this]{Refresh();});connect(reverse_,&QCheckBox::toggled,this,[this]{Refresh();});
    qApp->installEventFilter(this);
}
void V2CurvePointsTool::QueueRefresh(){if(!isVisible())return;points_.clear();confirm_->setEnabled(false);timer_->start();}
void V2CurvePointsTool::Refresh() {
    if(!isVisible())return;
    timer_->stop();points_.clear();confirm_->setEnabled(false);auto& view=window_.Viewport();view.HideToolPreview();
    pitch_->setVisible(mode_->currentIndex()==0);divisions_->setVisible(mode_->currentIndex()==1);
    const auto curves=app::SelectedCurves(view.Selection(),window_.Session().Scene());
    inputs_=view.Selection().ordered;
    if(inputs_.empty())for(const auto& id:view.Selection().entityIds){app::SelectionRef ref;ref.entityId=id;inputs_.push_back(ref);}
    std::erase_if(inputs_,[this](const auto& ref){const auto* e=window_.Session().GetDocument().FindEntity(ref.entityId);return !e||e->kind!=domain::EntityKind::Wire;});
    QStringList names;for(const auto& ref:inputs_){const auto* entity=window_.Session().GetDocument().FindEntity(ref.entityId);
        QString name=QString::fromStdString(entity->displayName);
        if(ref.segmentId){int index=0;for(const auto& edge:window_.Session().Scene().curves)if(edge.entityId==ref.entityId){++index;if(edge.segmentId==*ref.segmentId)break;}
            name+=QStringLiteral(" / 辺%1").arg(index);}else name+=QStringLiteral(" / 全体");names.append(name);}
    role_->SetItems(names);role_->value->setText(QStringLiteral("%1辺を選択").arg(curves.size()));
    if(curves.empty()){status_->setText(QStringLiteral("① の辺を3Dビューまたは左ペインで選んでください。押し直すとその辺を外します。"));return;}
    const auto add=[this](geometry::Vector3 p){for(const auto& old:points_)if(geometry::Distance(old,p)<accuracy)return;points_.push_back(p);};
    double totalLength=0;
    for(const auto& curve:curves){ArcPositions positions(curve);const double length=positions.Length();totalLength+=length;
        if(!std::isfinite(length)||length<=accuracy){points_.clear();status_->setText(QStringLiteral("長さが無効な辺があります。対象を選び直してください。"));return;}
        const double step=mode_->currentIndex()==0?pitch_->value():length/divisions_->value();
        const double count=std::max(0.0,std::ceil((length-accuracy)/step)-1);
        if(!std::isfinite(count)||count>double(limit)){points_.clear();status_->setText(QStringLiteral("1,000点を超えます。間隔を大きくするか等分数・対象を減らしてください。"));return;}
        if(endpoints_->isChecked())add(positions.At(reverse_->isChecked()?length:0));
        for(std::size_t i=1;i<=std::size_t(count);++i){double distance=double(i)*step;
            add(positions.At(reverse_->isChecked()?length-distance:distance));}
        if(endpoints_->isChecked())add(positions.At(reverse_->isChecked()?0:length));
        if(points_.size()>limit){points_.clear();status_->setText(QStringLiteral("合計1,000点を超えます。対象を減らすか間隔を大きくしてください。"));return;}
    }
    if(points_.size()>limit){points_.clear();status_->setText(QStringLiteral("点数が上限を超えています。条件を見直してください。"));return;}
    std::vector<std::vector<geometry::Vector3>> markers;
    const auto axis=view.WorkPlane().uAxis;const auto vertical=geometry::Cross(view.WorkPlane().normal,axis);
    const double radius=std::max(0.03,view.VisibleWidthMm()/600.0);
    for(const auto& p:points_){markers.push_back({p-axis*radius,p+axis*radius});markers.push_back({p-vertical*radius,p+vertical*radius});}
    view.ShowToolPreview(markers);confirm_->setEnabled(!points_.empty());
    status_->setText(QStringLiteral("下見 %1点／%2辺。辺の長さ合計 %3 mm。Enterで確定、Escで入力取消。")
        .arg(points_.size()).arg(curves.size()).arg(totalLength,0,'f',3));
}
void V2CurvePointsTool::Confirm() {
    pitch_->interpretText();divisions_->interpretText();Refresh();if(points_.empty())return;
    auto& document=window_.Session().GetDocument();AddCurvePoints batch;
    batch.commands.reserve(points_.size());
    std::size_t index=0;
    for(const auto& p:points_){domain::Feature feature;feature.id=window_.ids_->NextTyped<base::IdKind::Feature>();feature.type=domain::FeatureType::CreatePoint;
        feature.displayName="等間隔の点 "+std::to_string(++index);domain::CreatePointDefinition definition;definition.positionMm=p;feature.definition=definition;
        domain::Entity entity;entity.id=window_.ids_->NextTyped<base::IdKind::Entity>();entity.createdBy=feature.id;entity.kind=domain::EntityKind::Point;
        entity.displayName=feature.displayName;
        feature.outputs.push_back({"point",entity.id,entity.kind});
        batch.commands.emplace_back(feature,std::vector<domain::Entity>{entity},feature.displayName);}
    const auto result=document.Run(batch);
    if(!result.committed){window_.ReportDiagnostics(result.diagnostics);return;}
    const auto count=points_.size();window_.AdoptCurrentDocument();window_.Viewport().SetSelection({});Refresh();
    status_->setText(QStringLiteral("%1点を作成しました。1回のUndoで全点を戻せます。次の辺を選んで続けられます。").arg(count));
}
bool V2CurvePointsTool::eventFilter(QObject* object,QEvent* event) {
    if(!isVisible()||V2ToolSelection::Active(window_.Viewport()))return false;
    auto* widget=qobject_cast<QWidget*>(object);if(!widget||widget->window()!=window())return false;
    if(event->type()==QEvent::KeyPress){
        // Tree labels, searches and other panels own their typing keys.
        if(widget!=&window_.Viewport() && widget!=this && !isAncestorOf(widget))return false;
        const auto key=static_cast<QKeyEvent*>(event)->key();
        if(key==Qt::Key_Escape){window_.Viewport().SetSelection({});Refresh();return true;}
        if(key==Qt::Key_Return||key==Qt::Key_Enter){
            auto* spin=dynamic_cast<QAbstractSpinBox*>(widget);if(!spin)spin=dynamic_cast<QAbstractSpinBox*>(widget->parentWidget());
            if(spin&&!spin->hasAcceptableInput()){status_->setText(QStringLiteral("数値入力を完成させてください。まだ確定していません。"));return true;}
            if(auto* numeric=dynamic_cast<QDoubleSpinBox*>(spin)){const double before=numeric->value();numeric->interpretText();if(before!=numeric->value()){Refresh();return true;}}
            if(auto* integer=dynamic_cast<QSpinBox*>(spin)){const int before=integer->value();integer->interpretText();if(before!=integer->value()){Refresh();return true;}}
            Confirm();return true;}}
    if(object==&window_.Viewport()&&event->type()==QEvent::MouseButtonRelease)timer_->start(0);
    if(object==window_.entityTree_ && event->type()==QEvent::MouseButtonRelease)timer_->start(0);
    return false;
}
void V2CurvePointsTool::closeEvent(QCloseEvent* event) {
    timer_->stop();window_.Viewport().HideToolPreview();window_.Viewport().SetToolPickActive(false);window_.Viewport().SetToolPickToggle(false);QWidget::closeEvent(event);
}
