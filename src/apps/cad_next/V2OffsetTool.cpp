#include "V2NumericSpinBox.h"
#include <QString>
#include <QWidget>
#include "V2OffsetTool.h"
#include "V2WireEditEvaluation.h"
#include "V2DrawingPlaneProjection.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SurfacePreview.h"
#include "kachakacha/document/FeatureReevaluation.h"
#include "kachakacha/geometry/CurveProjection.h"
#include "kachakacha/geometry/CurveSampling.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QCloseEvent>
using namespace kachakacha::v2;
namespace {
V2OffsetTool* Active(V2MainWindow& w){return dynamic_cast<V2OffsetTool*>(w.OperationHost().TemporaryPage());}
base::Result<std::vector<geometry::CurveSegment>> PreviewOffsets(V2MainWindow& w,
    const domain::TransformWireDefinition& definition,V2DrawingProjectionStats* stats=nullptr) {
    using Out=base::Result<std::vector<geometry::CurveSegment>>;
    const auto& selection=w.Viewport().Selection();
    const auto tolerance=w.Session().GetDocument().Snapshot().settings.tolerance;
    const auto evaluate=[&](const auto& curves) {
        V2DrawingProjectionStats part;
        const auto prepared=V2OffsetTool::PrepareCurves(w,curves,stats?&part:nullptr);
        if(stats){
            const double squares=stats->rmsErrorMm*stats->rmsErrorMm*stats->samples+part.rmsErrorMm*part.rmsErrorMm*part.samples;
            stats->samples+=part.samples;stats->maximumErrorMm=std::max(stats->maximumErrorMm,part.maximumErrorMm);
            stats->rmsErrorMm=stats->samples?std::sqrt(squares/stats->samples):0;
        }
        return prepared.HasValue()?v2ui::EvaluateWireEdit(definition,prepared.Value(),tolerance):Out::Failure(prepared.Diagnostics());
    };
    if(selection.entityIds.size()<=1) return evaluate(app::SelectedCurves(selection,w.Session().Scene()));
    std::vector<geometry::CurveSegment> combined;
    std::vector<base::Diagnostic> diagnostics;
    // Confirmation offsets each selected wire separately. Preview the same groups,
    // rather than accidentally joining edges from different objects into a contour.
    for(const auto id:selection.entityIds){
        app::SelectionSet one;one.entityIds={id};
        for(const auto& ref:selection.ordered)if(ref.entityId==id)one.ordered.push_back(ref);
        const auto curves=app::SelectedCurves(one,w.Session().Scene());if(curves.empty())continue;
        const auto made=evaluate(curves);
        if(!made.HasValue())return Out::Failure(made.Diagnostics());
        combined.insert(combined.end(),made.Value().begin(),made.Value().end());
        diagnostics.insert(diagnostics.end(),made.Diagnostics().begin(),made.Diagnostics().end());
    }
    return Out::Success(std::move(combined),std::move(diagnostics));
}
}
void V2OffsetTool::Open(V2MainWindow& w) {
    if(Active(w))return;
    w.RunCommand("selection.activate");
    auto* tool=new V2OffsetTool(w);
    w.OperationHost().ShowTemporaryPage(tool,QStringLiteral("オフセット"));
    w.Viewport().SetToolPickActive(true);w.Viewport().SetToolPickToggle(true);tool->Preview();
}
V2OffsetTool::V2OffsetTool(V2MainWindow& w):QWidget(&w),window_(w) {
    setObjectName(QStringLiteral("offsetPlanePanel"));auto* layout=new QVBoxLayout(this);
    auto* hint=new QLabel(QStringLiteral("対象の線を続けて選択 → 距離と加工平面を確認 → Enter。作図面は変更しません。"),this);
    hint->setWordWrap(true);layout->addWidget(hint);
    layout->addWidget(new QLabel(QStringLiteral("加工平面"),this));
    plane_=new QComboBox(this);plane_->setObjectName(QStringLiteral("offsetPlaneChoice"));
    plane_->addItem(QStringLiteral("選んだ線の平面（元の位置）"));plane_->addItem(QStringLiteral("現在の作図面へ投影して加工"));
    for(const auto& entity:w.Session().GetDocument().Snapshot().entities)if(entity.kind==domain::EntityKind::WorkPlane)
        plane_->addItem(QString::fromStdString(entity.displayName),QString::fromStdString(entity.id.ToString()));
    if(const auto* scope=w.findChild<QComboBox*>("drawingScope");scope&&scope->currentIndex()==0)plane_->setCurrentIndex(1);
    layout->addWidget(plane_);layout->addWidget(new QLabel(QStringLiteral("距離（符号で反転）"),this));
    distance_=new V2NumericSpinBox(this);distance_->setObjectName(QStringLiteral("offsetDistance"));
    distance_->setRange(-1000,1000);distance_->setDecimals(3);distance_->setSuffix(QStringLiteral(" mm"));
    distance_->setKeyboardTracking(false);
    distance_->setValue(app::ParameterValueOf(w.ParameterDock().Values(),app::ParameterId::OffsetDistanceMm));layout->addWidget(distance_);
    status_=new QLabel(this);status_->setObjectName(QStringLiteral("offsetStatus"));status_->setWordWrap(true);layout->addWidget(status_);layout->addStretch();
    auto* confirm=new QPushButton(QStringLiteral("確定 Enter"),this);confirm->setObjectName(QStringLiteral("offsetConfirm"));layout->addWidget(confirm);
    auto* clear=new QPushButton(QStringLiteral("入力を取り消す Esc"),this);layout->addWidget(clear);
    auto* done=new QPushButton(QStringLiteral("ツール一覧に戻る"),this);layout->addWidget(done);
    connect(plane_,&QComboBox::currentIndexChanged,this,[this]{Preview();});
    connect(distance_,&QDoubleSpinBox::valueChanged,this,[this]{Preview();});
    connect(confirm,&QPushButton::clicked,this,[this]{Confirm();});
    connect(clear,&QPushButton::clicked,this,[this]{Key(window_,Qt::Key_Escape);});
    connect(done,&QPushButton::clicked,this,[this]{window_.RunCommand("selection.activate");});
}
base::Result<std::vector<geometry::CurveSegment>> V2OffsetTool::PrepareCurves(V2MainWindow& w,const std::vector<geometry::CurveSegment>& curves,V2DrawingProjectionStats* stats) {
    using Out=base::Result<std::vector<geometry::CurveSegment>>;
    const auto* tool=Active(w);if(!tool||tool->plane_->currentIndex()==0)return Out::Success(curves);
    auto frame=w.Viewport().WorkPlane();
    if(tool->plane_->currentIndex()>=2){
        const auto parsed=base::Uuid::Parse(tool->plane_->currentData().toString().toStdString());
        const auto chosen=parsed?w.WorkPlaneFrameOf(base::EntityId(*parsed)):std::nullopt;
        if(!chosen)return Out::Failure(base::MakeError("GEO-E005","指定した加工平面がありません。","別の候補を選んでください。"));
        frame=*chosen;
    }
    return V2ProjectDrawingCurves(curves,frame,w.Session().GetDocument().Snapshot().settings.tolerance,stats);
}
base::Result<modeling::WorkPlaneFrame> V2OffsetTool::PlaneFor(V2MainWindow& w,const std::vector<geometry::CurveSegment>& curves) {
    auto frame=w.Viewport().WorkPlane();auto* tool=Active(w);const int index=tool?tool->plane_->currentIndex():0;
    if(index==0&&curves.size()==1&&curves.front().Kind()==geometry::CurveKind::Line){
        const auto whole=app::SelectedWholeCurves(w.Viewport().Selection(),w.Session().Scene());
        if(whole.size()>1){const auto inferred=app::EditingPlane(whole,frame);if(inferred.HasValue())frame=inferred.Value();}
    }
    if(index>=2){
        const auto parsed=base::Uuid::Parse(tool->plane_->currentData().toString().toStdString());
        const auto chosen=parsed?w.WorkPlaneFrameOf(base::EntityId(*parsed)):std::nullopt;
        if(!chosen)return base::Result<modeling::WorkPlaneFrame>::Failure(base::MakeError("GEO-E005","指定した加工平面がありません。","別の候補を選んでください。"));
        frame=*chosen;
    }
    const auto prepared=PrepareCurves(w,curves);
    if(!prepared.HasValue())return base::Result<modeling::WorkPlaneFrame>::Failure(prepared.Diagnostics());
    return app::EditingPlane(prepared.Value(),frame,index==0);
}
void V2OffsetTool::Refresh(V2MainWindow& w){if(auto* tool=Active(w))tool->Preview();}
void V2OffsetTool::Preview() {
    auto& view=window_.Viewport();view.HideToolPreview();
    status_->setToolTip({});
    const auto inputs=app::SelectedCurves(view.Selection(),window_.Session().Scene());
    const auto tolerance=window_.Session().GetDocument().Snapshot().settings.tolerance;
    const auto plane=PlaneFor(window_,inputs);
    if(!plane.HasValue()){status_->setText(QString::fromStdString(plane.FirstSummaryJa())+QStringLiteral(" 平面候補を変更すると3Dで確認できます。"));return;}
    domain::TransformWireDefinition d;d.method=domain::WireTransformMethod::Offset;d.vectorArgument=plane.Value().normal;d.scalarArgument.value=distance_->value();
    V2DrawingProjectionStats projection;
    const auto made=PreviewOffsets(window_,d,&projection);
    if(!made.HasValue()){status_->setText(QString::fromStdString(made.FirstSummaryJa()));return;}
    view.ShowToolPreview(app::SurfaceBoundaryLines(made.Value(),24));
    auto text=QStringLiteral("加工平面：%1。%2辺の下見。Enterで確定、Escで入力だけ取り消します。")
        .arg(plane_->currentText()).arg(made.Value().size());
    if(plane_->currentIndex()!=0){
        double maximum=0,squares=0;std::size_t count=0;
        for(const auto& curve:inputs)for(const auto& sample:geometry::SampleCurve(curve,tolerance.modelLinearMm,256)){
            const double move=std::abs(geometry::Dot(sample.position-plane.Value().origin,plane.Value().normal));
            maximum=std::max(maximum,move);squares+=move*move;++count;
        }
        text+=QStringLiteral("\n元の線は保持。平面への移動（標本）：最大 %1 mm / RMS %2 mm").arg(maximum,0,'g',8).arg(count?std::sqrt(squares/count):0,0,'g',8);
    }
    QString details;
    if(projection.samples)text+=QStringLiteral("\n平面投影の曲線近似：偏差上限 %1 mm / RMS（標本）%2 mm")
        .arg(projection.maximumErrorMm,0,'g',8).arg(projection.rmsErrorMm,0,'g',8);
    for(const auto& diagnostic:made.Diagnostics()) {
        const auto line=QString::fromStdString(diagnostic.summaryJa);
        details+=line+QStringLiteral("\n");
        if(details.count('\n')<=4)text+=QStringLiteral("\n")+line;
    }
    if(made.Diagnostics().size()>4)text+=QStringLiteral("\n残り%1件の計測値はここにポインターを置いて確認できます。").arg(made.Diagnostics().size()-4);
    status_->setToolTip(details);status_->setText(text);
}
void V2OffsetTool::Confirm() {
    if(app::SelectedCurves(window_.Viewport().Selection(),window_.Session().Scene()).empty()){Preview();return;}
    distance_->interpretText();
    const auto inputs=app::SelectedCurves(window_.Viewport().Selection(),window_.Session().Scene());
    const auto plane=PlaneFor(window_,inputs);if(!plane.HasValue()){Preview();return;}
    domain::TransformWireDefinition d;d.method=domain::WireTransformMethod::Offset;d.vectorArgument=plane.Value().normal;d.scalarArgument.value=distance_->value();
    if(!PreviewOffsets(window_,d).HasValue()){Preview();return;}
    if(!window_.ParameterDock().Apply(app::ParameterId::OffsetDistanceMm,QString::number(distance_->value()))){status_->setText(QStringLiteral("距離を適用できません。値を見直してください。"));return;}
    const auto revision=window_.Session().GetDocument().Revision();window_.RunCommand("wire.offset");
    if(window_.Session().GetDocument().Revision()!=revision)window_.Viewport().SetSelection({});
    Preview();
}
bool V2OffsetTool::Key(V2MainWindow& w,int key) {
    auto* tool=Active(w);if(!tool)return false;
    if(key==Qt::Key_Return||key==Qt::Key_Enter){tool->Confirm();return true;}
    if(key==Qt::Key_Escape){w.Viewport().SetSelection({});w.Viewport().HideToolPreview();tool->Preview();return true;}
    return false;
}
void V2OffsetTool::closeEvent(QCloseEvent* e) {
    window_.Viewport().HideToolPreview();window_.Viewport().SetToolPickActive(false);window_.Viewport().SetToolPickToggle(false);QWidget::closeEvent(e);
}
