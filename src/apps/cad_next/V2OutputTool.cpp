#include <QDialog>
#include <QObject>
#include <QString>
#include "V2OutputTool.h"
#include "V2OutputPreview.h"
#include "V2MainWindow.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>
using namespace kachakacha::v2;
V2OutputTool* V2OutputTool::Open(V2MainWindow& window,int format) {
    for(auto* child:window.findChildren<QDialog*>())if(child->objectName()=="selectionOutput")child->close();
    window.EndArmedTools();window.SelectTool(modeling::DrawingTool::Select);
    auto* dialog=new V2OutputTool(window,format);dialog->TakeSelection();dialog->show();return dialog;
}
V2OutputTool::V2OutputTool(V2MainWindow& window,int format):QDialog(&window),window_(window) {
    setObjectName("selectionOutput");setWindowTitle(QStringLiteral("選択を出力・配置"));setAttribute(Qt::WA_DeleteOnClose);resize(1120,780);
    BuildUi(format);window_.viewport_->installEventFilter(this);
    QObject::connect(this,&QDialog::finished,this,[this]{if(pickRole_==1)window_.viewport_->CancelPointPick();pickRole_=0;});
}
void V2OutputTool::BuildUi(int format) {
    auto* root=new QVBoxLayout(this);auto* row=new QHBoxLayout();auto* settings=new QVBoxLayout();
    auto* take=new QPushButton(QStringLiteral("3D／左ペインの選択を取り込む"),this);take->setObjectName("outputTakeSelection");settings->addWidget(take);
    auto* scope=new QComboBox(this);scope->setObjectName("outputScope");scope->addItems({QStringLiteral("選択した物体全体"),QStringLiteral("選択した面・辺・点だけ")});settings->addWidget(scope);
    QObject::connect(scope,&QComboBox::currentIndexChanged,this,[this]{TakeSelection();});
    targets_=new QListWidget(this);targets_->setObjectName("outputTargets");targets_->setMaximumHeight(145);settings->addWidget(targets_);
    format_=new QComboBox(this);format_->setObjectName("outputFormat");format_->addItems({QStringLiteral("STL（面の近似三角形）"),QStringLiteral("STEP（立体・面・線）"),QStringLiteral("別KCD2（現在形状の独立コピー）"),QStringLiteral("同一KCD（現在形状の独立コピー）")});
    format_->setCurrentIndex(format<0?2:format);settings->addWidget(format_);BuildPlacement(settings);
    settings->addWidget(new QLabel(QStringLiteral("元への追従はしません。厚みのない面には厚みを追加しません。"),this));
    status_=new QLabel(this);status_->setWordWrap(true);settings->addWidget(status_);settings->addStretch();
    row->addLayout(settings);auto* right=new QVBoxLayout();preview_=new V2OutputPreview(this);right->addWidget(preview_,1);
    right->addWidget(new QLabel(QStringLiteral("出力形状プレビュー：ドラッグで回転、中ドラッグで移動、ホイールで拡大"),this));
    auto* fit=new QPushButton(QStringLiteral("全体表示"),this);right->addWidget(fit);row->addLayout(right,1);root->addLayout(row,1);
    auto* actions=new QHBoxLayout();auto* cancel=new QPushButton(QStringLiteral("取消"),this);save_=new QPushButton(QStringLiteral("出力を確定…"),this);save_->setObjectName("outputConfirm");actions->addWidget(cancel);actions->addWidget(save_);root->addLayout(actions);
    QObject::connect(take,&QPushButton::clicked,this,[this]{TakeSelection();});
    QObject::connect(targets_,&QListWidget::itemChanged,this,[this]{if(!loading_)RefreshPreview();});
    QObject::connect(format_,&QComboBox::currentIndexChanged,this,[this]{RefreshPreview();});
    QObject::connect(fit,&QPushButton::clicked,preview_,[this]{preview_->Fit();});
    QObject::connect(cancel,&QPushButton::clicked,this,[this]{close();});
    QObject::connect(save_,&QPushButton::clicked,this,[this]{Run();});
}
void V2OutputTool::ChooseFormat(int format){format_->setCurrentIndex(format);}
void V2OutputTool::SetProblem(const QString& text){status_->setText(text);save_->setEnabled(false);preview_->SetModels({});}
void V2OutputTool::TakeSelection() {
    generated_=false;
    if(!Capture(window_.viewport_->Selection().entityIds) || (findChild<QComboBox*>("outputScope")->currentIndex()==1 && !CaptureElements())){assets_.clear();loading_=true;targets_->clear();loading_=false;return;}
    revision_=window_.session_->GetDocument().Revision();documentId_=window_.session_->GetDocument().Snapshot().id;FillTargets();
}
void V2OutputTool::FillTargets() {
    loading_=true;targets_->clear();
    for(const auto& asset:assets_){auto* item=new QListWidgetItem(QString::fromStdString(asset.entity.displayName)+QStringLiteral(" / ")+QString::fromUtf8(domain::EntityKindNameJa(asset.entity.kind).data()),targets_);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(Qt::Checked);}
    loading_=false;RefreshPreview();preview_->Fit();
}
bool V2OutputTool::Capture(const std::vector<base::EntityId>& ids) {
    assets_.clear();if(ids.empty()){SetProblem(QStringLiteral("3Dビューまたは左ペインで出すものを選び、選択を取り込んでください。"));return false;}
    for(const auto& id:ids){const auto* entity=window_.session_->GetDocument().FindEntity(id);if(entity!=nullptr && entity->kind==domain::EntityKind::FabricationModel)return CaptureModels(ids);}
    auto snapshot=window_.session_->GetDocument().Snapshot();for(auto& entity:snapshot.entities)entity.visibility=domain::Visibility::Visible;
    const auto scene=app::BuildSceneFromDocument(snapshot,*window_.ids_);
    for(const auto& id:ids){const auto* entity=window_.session_->GetDocument().FindEntity(id);
        if(entity==nullptr){SetProblem(QStringLiteral("選んだ対象が文書にありません。"));return false;}
        V2OutputAsset asset;asset.entity=*entity;
        if(entity->kind==domain::EntityKind::Part || entity->kind==domain::EntityKind::GuideSurface){
            const auto& shapes=entity->kind==domain::EntityKind::Part?window_.partShapes_:window_.guideShapes_;
            const auto found=shapes.find(id.ToString());if(found==shapes.end()){SetProblem(QString::fromStdString(entity->displayName)+QStringLiteral(": 実形状がありません。"));return false;}asset.shape=found->second;
        }else if(entity->kind==domain::EntityKind::Wire){for(const auto& curve:scene.curves)if(curve.entityId==id)asset.curves.push_back(curve.segment);
            if(asset.curves.empty()){SetProblem(QString::fromStdString(entity->displayName)+QStringLiteral(": 曲線を評価できません。"));return false;}
        }else if(entity->kind==domain::EntityKind::Point){bool found=false;for(const auto& point:scene.points)if(point.entityId==id){asset.frame.origin=point.position;found=true;}if(!found){SetProblem(QStringLiteral("点を評価できません。"));return false;}
        }else if(entity->kind==domain::EntityKind::WorkPlane){const auto frame=window_.WorkPlaneFrameOf(id);if(!frame.has_value()){SetProblem(QStringLiteral("作業平面を評価できません。"));return false;}asset.frame={frame->origin,frame->normal,frame->uAxis};
        }else {SetProblem(QString::fromStdString(entity->displayName)+QStringLiteral(": 製作の生成で現在形状を取り出してから選んでください。"));return false;}
        assets_.push_back(std::move(asset));
    }
    return true;
}
bool V2OutputTool::Prepare() {
    prepared_.clear();const auto placement=Placement();if(!placement.Valid()){SetProblem(QStringLiteral("配置の法線と面内方向は、長さ0や平行にできません。"));return false;}
    for(int i=0;i<targets_->count();++i){if(targets_->item(i)->checkState()!=Qt::Checked)continue;auto asset=assets_[static_cast<std::size_t>(i)];
        if(!asset.curves.empty()){const auto curves=kernel::PlaceOutputCurves(asset.curves,placement,1e-7);if(!curves.HasValue()){SetProblem(QString::fromStdString(curves.FirstSummaryJa()));return false;}asset.curves=curves.Value();}
        if(asset.entity.kind==domain::EntityKind::Part || asset.entity.kind==domain::EntityKind::GuideSurface){const auto shape=kernel::PlaceOutputShape(asset.shape,placement);if(!shape.HasValue()){SetProblem(QString::fromStdString(shape.FirstSummaryJa()));return false;}asset.shape=shape.Value();}
        asset.frame.origin=placement.Point(asset.frame.origin);asset.frame.normal=placement.Direction(asset.frame.normal);asset.frame.xDirection=placement.Direction(asset.frame.xDirection);prepared_.push_back(std::move(asset));
    }
    if(prepared_.empty()){SetProblem(QStringLiteral("出す対象にチェックしてください。"));return false;}return true;
}
void V2OutputTool::RefreshPreview() {
    if(loading_||status_==nullptr)return;
    if(!Prepare())return;
    std::vector<modeling::ShapeMesh> meshes;QString notes;
    for(const auto& asset:prepared_){modeling::ShapeMesh mesh;
        if(asset.entity.kind==domain::EntityKind::Part || asset.entity.kind==domain::EntityKind::GuideSurface){const auto made=kernel::BuildShapeMesh(asset.shape,0.01);if(!made.HasValue()){SetProblem(QString::fromStdString(made.FirstSummaryJa()));return;}mesh=made.Value();if(asset.entity.kind==domain::EntityKind::GuideSurface)notes=QStringLiteral("厚みのない面を含みます。STLでも厚みはありません。");}
        else {geometry::Bounds3 bounds;for(const auto& c:asset.curves){std::vector<geometry::Vector3> edge;for(int k=0;k<=64;++k){edge.push_back(c.Evaluate(k/64.0));bounds.Add(edge.back());}mesh.edges.push_back(edge);}if(asset.curves.empty()){mesh.edges.push_back({asset.frame.origin});bounds.Add(asset.frame.origin);}mesh.minimum=bounds.minimum;mesh.maximum=bounds.maximum;
            if(format_->currentIndex()==0){SetProblem(QString::fromStdString(asset.entity.displayName)+QStringLiteral(": 線・点・作業平面はSTLにできません。対象を外すか形式を変更してください。"));return;}
            if(format_->currentIndex()==1 && asset.curves.empty()){SetProblem(QString::fromStdString(asset.entity.displayName)+QStringLiteral(": STEPには形状のある対象を選んでください。"));return;}}
        meshes.push_back(std::move(mesh));}
    preview_->SetModels(std::move(meshes));save_->setEnabled(true);save_->setText(format_->currentIndex()==3?QStringLiteral("同一KCDへコピーを作成"):QStringLiteral("保存先を選んで出力…"));status_->setText(QStringLiteral("%1個の出力形状。%2").arg(prepared_.size()).arg(notes));
}
void V2OutputTool::Run(){if(format_->currentIndex()==3){if(SaveTo(QString()))close();return;}
    const QString extension=format_->currentIndex()==0?QStringLiteral("stl"):format_->currentIndex()==1?QStringLiteral("step"):QStringLiteral("kcd2");
    QFileDialog dialog(this,QStringLiteral("出力先"));dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(QStringLiteral("出力 (*.%1)").arg(extension));dialog.setDefaultSuffix(extension);
    if(dialog.exec()!=QDialog::Accepted || dialog.selectedFiles().isEmpty())return;
    if(SaveTo(dialog.selectedFiles().front()))close();}
