#include <QString>
#include <QWidget>
#include "V2OffsetTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SurfacePreview.h"
#include "kachakacha/document/FeatureReevaluation.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QCloseEvent>
using namespace kachakacha::v2;
namespace {
V2OffsetTool* Active(V2MainWindow& w){return dynamic_cast<V2OffsetTool*>(w.OperationHost().TemporaryPage());}
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
    plane_->addItem(QStringLiteral("自動（選んだ線から判定）"));plane_->addItem(QStringLiteral("現在の作図面の向き"));
    for(const auto& entity:w.Session().GetDocument().Snapshot().entities)if(entity.kind==domain::EntityKind::WorkPlane)
        plane_->addItem(QString::fromStdString(entity.displayName),QString::fromStdString(entity.id.ToString()));
    layout->addWidget(plane_);layout->addWidget(new QLabel(QStringLiteral("距離（符号で反転）"),this));
    distance_=new QDoubleSpinBox(this);distance_->setObjectName(QStringLiteral("offsetDistance"));
    distance_->setRange(-1000,1000);distance_->setDecimals(3);distance_->setSuffix(QStringLiteral(" mm"));
    distance_->setValue(app::ParameterValueOf(w.ParameterDock().Values(),app::ParameterId::OffsetDistanceMm));layout->addWidget(distance_);
    status_=new QLabel(this);status_->setWordWrap(true);layout->addWidget(status_);layout->addStretch();
    auto* confirm=new QPushButton(QStringLiteral("確定 Enter"),this);confirm->setObjectName(QStringLiteral("offsetConfirm"));layout->addWidget(confirm);
    auto* clear=new QPushButton(QStringLiteral("入力を取り消す Esc"),this);layout->addWidget(clear);
    auto* done=new QPushButton(QStringLiteral("ツール一覧に戻る"),this);layout->addWidget(done);
    connect(plane_,&QComboBox::currentIndexChanged,this,[this]{Preview();});
    connect(distance_,&QDoubleSpinBox::valueChanged,this,[this]{Preview();});
    connect(confirm,&QPushButton::clicked,this,[this]{Confirm();});
    connect(clear,&QPushButton::clicked,this,[this]{Key(window_,Qt::Key_Escape);});
    connect(done,&QPushButton::clicked,this,[this]{window_.RunCommand("selection.activate");});
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
    return app::EditingPlane(curves,frame,index==0);
}
void V2OffsetTool::Refresh(V2MainWindow& w){if(auto* tool=Active(w))tool->Preview();}
void V2OffsetTool::Preview() {
    auto& view=window_.Viewport();view.HideToolPreview();
    const auto inputs=app::SelectedCurves(view.Selection(),window_.Session().Scene());
    const auto plane=PlaneFor(window_,inputs);
    if(!plane.HasValue()){status_->setText(QString::fromStdString(plane.FirstSummaryJa())+QStringLiteral(" 平面候補を変更すると3Dで確認できます。"));return;}
    domain::TransformWireDefinition d;d.method=domain::WireTransformMethod::Offset;d.vectorArgument=plane.Value().normal;d.scalarArgument.value=distance_->value();
    const auto made=document::EvaluateWireTransform(d,inputs);
    if(!made.HasValue()){status_->setText(QString::fromStdString(made.FirstSummaryJa()));return;}
    view.ShowToolPreview(app::SurfaceBoundaryLines(made.Value(),24));
    status_->setText(QStringLiteral("加工平面：%1。%2辺の下見。Enterで確定、Escで入力だけ取り消します。")
        .arg(plane_->currentText()).arg(inputs.size()));
}
void V2OffsetTool::Confirm() {
    if(app::SelectedCurves(window_.Viewport().Selection(),window_.Session().Scene()).empty()){Preview();return;}
    distance_->interpretText();
    const auto inputs=app::SelectedCurves(window_.Viewport().Selection(),window_.Session().Scene());
    const auto plane=PlaneFor(window_,inputs);if(!plane.HasValue()){Preview();return;}
    domain::TransformWireDefinition d;d.method=domain::WireTransformMethod::Offset;d.vectorArgument=plane.Value().normal;d.scalarArgument.value=distance_->value();
    if(!document::EvaluateWireTransform(d,inputs).HasValue()){Preview();return;}
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
