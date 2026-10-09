#include "V2DimensionTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include "kachakacha/document/Commands.h"
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QString>
#include <QWidget>
#include <algorithm>
using namespace kachakacha::v2;
V2DimensionTool* V2DimensionTool::Open(V2MainWindow& window)
{
    if(auto* existing=dynamic_cast<V2DimensionTool*>(window.operationHost_->TemporaryPage()))return existing;
    const auto selection=window.viewport_->Selection();
    window.EndArmedTools();window.ClearPendingCommand();window.SelectTool(modeling::DrawingTool::Select);
    window.operationHost_->SetShelves({});window.viewport_->SetSelection(selection);
    auto* tool=new V2DimensionTool(window);
    window.operationHost_->ShowTemporaryPage(tool,QStringLiteral("寸法"));
    tool->TakeSelection();return tool;
}
V2DimensionTool::V2DimensionTool(V2MainWindow& window):QWidget(&window),window_(window),
    documentId_(window.Session().GetDocument().Snapshot().id)
{
    setObjectName(QStringLiteral("dimensionTool"));BuildUi();Reset();RefreshList();qApp->installEventFilter(this);
}
void V2DimensionTool::Reset()
{
    dimension_={};fresh_=true;placing_=false;
    dimension_.dimensionU=window_.viewport_->WorkPlane().uAxis;
    dimension_.dimensionV=window_.viewport_->WorkPlane().vAxis;
    window_.viewport_->SetDimensionPreview({});
    UpdateKind();status_->setText(QStringLiteral("作図線を選んでください。角度・2点間は2本選びます。"));
}
void V2DimensionTool::UpdateKind()
{
    static const char* kinds[]={"dim_length","dim_horizontal","dim_vertical","dim_radius","dim_diameter","dim_angle","dim_length","dim_horizontal","dim_vertical"};
    dimension_.kind=kinds[std::clamp(kind_->currentIndex(),0,8)];
    dimension_.unit=dimension_.kind=="dim_angle" ? "rad" : "mm";
    value_->setMaximum(dimension_.unit=="rad" ? 180.0 : 1000000.0);
    value_->setSuffix(dimension_.unit=="rad" ? QStringLiteral(" °") : QStringLiteral(" mm"));
    RefreshPreview();
}
bool V2DimensionTool::AddTarget(const app::SelectionRef& ref)
{
    if(!ref.segmentId){status_->setText(QStringLiteral("寸法を付ける作図線を選んでください。"));return false;}
    if(placing_){dimension_.segments.clear();dimension_.targets.clear();placing_=false;}
    auto& refs=dimension_.segments;
    if(std::any_of(refs.begin(),refs.end(),[&](const auto& r){return r.entityId==ref.entityId && r.segmentId==*ref.segmentId;}))return false;
    const double end=ref.curveParameter.value_or(0.0)<0.5 ? 0.0 : 1.0;
    refs.push_back({ref.entityId,*ref.segmentId,end,end});dimension_.targets.push_back(ref.entityId);
    const int needed=(kind_->currentIndex()>=5) ? 2 : 1;
    if(static_cast<int>(refs.size())>needed){refs.erase(refs.begin());dimension_.targets.erase(dimension_.targets.begin());}
    if(static_cast<int>(refs.size())==needed) {
        const auto current=document::EvaluateDimension(window_.session_->GetDocument().Snapshot(),dimension_);
        if(!current.HasValue()){status_->setText(QString::fromStdString(current.FirstSummaryJa()));return false;}
        const QSignalBlocker blocker(value_);
        value_->setValue(current.Value().value*(dimension_.unit=="rad" ? 180/3.141592653589793 : 1));
        placing_=true;
    }
    RefreshPreview();return true;
}
void V2DimensionTool::TakeSelection()
{
    const auto selected=window_.viewport_->Selection();
    std::vector<app::SelectionRef> refs;
    const auto expand=[&](base::EntityId id){
        for(const auto& curve:window_.session_->Scene().curves)if(curve.entityId==id) {
            app::SelectionRef ref;ref.entityId=id;ref.segmentId=curve.segmentId;refs.push_back(ref);
        }
    };
    for(const auto& ref:selected.ordered) {if(ref.segmentId)refs.push_back(ref);else expand(ref.entityId);}
    if(selected.ordered.empty())for(const auto& id:selected.entityIds)expand(id);
    const std::size_t needed=kind_->currentIndex()>=5 ? 2 : 1;
    if(refs.size()>needed){status_->setText(QStringLiteral("この寸法には%1本を選んでください。選択が多いため取り込みませんでした。").arg(needed));return;}
    for(const auto& ref:refs)AddTarget(ref);
}

void V2DimensionTool::RefreshPreview()
{
    if(loading_ || !status_)return;
    const auto current=document::EvaluateDimension(window_.session_->GetDocument().Snapshot(),dimension_);
    if(!current.HasValue()){window_.viewport_->SetDimensionPreview({});return;}
    dimension_.anchors=current.Value().anchors;
    const double shown=driving_->isChecked() ? value_->value() : current.Value().value*(dimension_.unit=="rad" ? 180/3.141592653589793 : 1);
    if(!driving_->isChecked()){const QSignalBlocker block(value_);value_->setValue(shown);}
    const QString prefix=dimension_.kind=="dim_radius" ? QStringLiteral("R ") : dimension_.kind=="dim_diameter" ? QStringLiteral("Ø ") : QString();
    const QString text=QStringLiteral("%1%2%3").arg((driving_->isChecked() ? QString() : QStringLiteral("参照 "))+prefix)
        .arg(shown,0,'f',3).arg(dimension_.unit=="rad" ? QStringLiteral("°") : QStringLiteral(" mm"));
    window_.viewport_->SetDimensionPreview(V2Viewport::KeptDimensionView{dimension_.anchors,text,dimension_.labelPosition,dimension_.id,
        dimension_.kind=="dim_horizontal" ? std::optional<geometry::Vector3>{dimension_.dimensionU}
        : dimension_.kind=="dim_vertical" ? std::optional<geometry::Vector3>{dimension_.dimensionV} : std::nullopt});
    status_->setText(QStringLiteral("値と配置位置を指定し、Enterで確定。空白のクリックでも配置・確定できます。Escは今回の入力だけ取消。"));
}
bool V2DimensionTool::Commit()
{
    if(window_.session_->GetDocument().Snapshot().id!=documentId_){status_->setText(QStringLiteral("文書が変わりました。寸法ツールを開き直してください。"));return false;}
    dimension_.driving=driving_->isChecked();dimension_.label=name_->text().toStdString();
    dimension_.recordedValue=value_->value()*(dimension_.unit=="rad" ? 3.141592653589793/180 : 1);
    if(!dimension_.labelPosition && dimension_.anchors.size()>=2) {
        const auto middle=(dimension_.anchors.front()+dimension_.anchors.back())*0.5;
        const auto mapping=window_.viewport_->Mapping();
        if(const auto screen=mapping.Project(middle)) {
            const auto ray=mapping.RayThrough(*screen);
            const double offset=24+20*(window_.session_->GetDocument().Snapshot().referenceDimensions.size()%5);
            if(ray)dimension_.labelPosition=mapping.UnprojectOntoPlane({screen->x+8,screen->y-offset},middle,ray->direction);
        }
    }
    if(dimension_.id.IsNil())dimension_.id=window_.ids_->NextTyped<base::IdKind::Dimension>();
    const auto result=window_.session_->GetDocument().Run(document::SetDimensionCommand(dimension_));
    if(!result.committed) {
        QString error;for(const auto& d:result.diagnostics)error+=QString::fromStdString(d.summaryJa+" "+d.detailsJa)+QStringLiteral("\n");
        status_->setText(error);return false;
    }
    window_.AdoptCurrentDocument();window_.RebuildKernelShapes();
    Reset();RefreshList();window_.SetStatus(QStringLiteral("寸法を設定しました。続けて次の線を選べます。"));return true;
}
void V2DimensionTool::RefreshList()
{
    const QSignalBlocker block(saved_);saved_->clear();saved_->addItem(QStringLiteral("新しい寸法"));
    for(const auto& dim:window_.session_->GetDocument().Snapshot().referenceDimensions)if(!dim.segments.empty())
        saved_->addItem(QString::fromStdString(dim.label.empty() ? dim.kind : dim.label),QString::fromStdString(dim.id.ToString()));
}
void V2DimensionTool::Load(base::DimensionId id)
{
    for(const auto& dim:window_.session_->GetDocument().Snapshot().referenceDimensions)if(dim.id==id && !dim.segments.empty()) {
        loading_=true;dimension_=dim;fresh_=false;
        const int kind=dim.kind=="dim_horizontal" ? (dim.segments.size()==2 ? 7 : 1) : dim.kind=="dim_vertical" ? (dim.segments.size()==2 ? 8 : 2) : dim.kind=="dim_radius" ? 3
            : dim.kind=="dim_diameter" ? 4 : dim.kind=="dim_angle" ? 5 : dim.segments.size()==2 ? 6 : 0;
        kind_->setCurrentIndex(kind);driving_->setChecked(dim.driving);name_->setText(QString::fromStdString(dim.label));
        const auto evaluated=document::EvaluateDimension(window_.session_->GetDocument().Snapshot(),dim);
        value_->setValue((evaluated.HasValue() ? evaluated.Value().value : dim.recordedValue)*(dim.unit=="rad" ? 180/3.141592653589793 : 1));
        loading_=false;placing_=true;UpdateKind();return;
    }
}
