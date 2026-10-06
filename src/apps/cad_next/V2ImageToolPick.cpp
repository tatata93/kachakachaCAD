#include <QObject>
#include <QString>
#include <QWidget>
#include "V2ImageTool.h"
#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"
#include "V2Viewport.h"
#include "kachakacha/kernel/OcctImage.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/modeling/MeshPick.h"
#include <QEvent>
#include <QImage>
#include <QCheckBox>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPointF>
#include <limits>
#include <cmath>
using namespace kachakacha::v2;
using namespace geometry;
void V2ImageTool::UseWorkPlane() {
    const auto frame=window_.WorkPlaneFrameOf(window_.ActiveWorkPlaneId());
    if(!frame){status_->setText(QStringLiteral("作業面を選んでください。"));return;}
    face_={};definition_.faceBrep.clear();definition_.origin=frame->origin;definition_.uAxis=frame->uAxis;definition_.vAxis=frame->vAxis;
    definition_.anchorUv={};definition_.uvMetric={1,1,0};definition_.followSurface=false;UpdateFields();
}
bool V2ImageTool::SetTarget(const app::SelectionRef& ref) {
    if(const auto frame=window_.WorkPlaneFrameOf(ref.entityId);frame){
        face_={};definition_.faceBrep.clear();definition_.origin=frame->origin;definition_.uAxis=frame->uAxis;definition_.vAxis=frame->vAxis;
        definition_.followSurface=false;UpdateFields();Preview();return true;}
    auto shape=window_.partShapes_.find(ref.entityId.ToString());modeling::KernelShapeHandle handle;
    if(shape!=window_.partShapes_.end()){if(!ref.pickedFaceIndex){status_->setText(QStringLiteral("3Dビューで貼付先のフェイスを選んでください。"));Pick(1);return false;}handle=shape->second;}
    else {auto guide=window_.guideShapes_.find(ref.entityId.ToString());if(guide!=window_.guideShapes_.end())handle=guide->second;}
    if(!handle.Valid()){status_->setText(QStringLiteral("作業面・面・ソリッドのフェイスを選んでください。"));return false;}
    const auto face=kernel::OutputFace(handle,ref.pickedFaceIndex.value_or(0));
    if(!face.HasValue()){status_->setText(QString::fromStdString(face.FirstSummaryJa()));return false;}
    const auto frame=kernel::OutputSurfaceFrame(face.Value(),ref.hitPoint);const auto brep=kernel::CaptureOutputShape(face.Value());
    if(!frame.HasValue()||!brep.HasValue()){status_->setText(QStringLiteral("貼付先の面を読み取れません。"));return false;}
    face_=face.Value();definition_.faceBrep=brep.Value();definition_.uAxis=Normalized(frame.Value().xDirection);
    definition_.vAxis=Normalized(Cross(Normalized(frame.Value().normal),definition_.uAxis));
    if(!SetAnchor(ref.hitPoint))return false;UpdateFields();Preview();return true;
}
bool V2ImageTool::SetAnchor(const Vector3& point) {
    if(face_.Valid()) {const auto projected=kernel::ImagePointOnSurface(face_,point);
        if(!projected.HasValue()){status_->setText(QString::fromStdString(projected.FirstSummaryJa()));return false;}
        definition_.origin=projected.Value().point;definition_.anchorUv=projected.Value().uv;definition_.uvMetric=projected.Value().metric;
    }else definition_.origin=point;
    return true;
}
void V2ImageTool::CancelInput() {
    dragging_=false;role_=0;pixels_.clear();points_.clear();imageMarks_.clear();imageHover_.reset();
    window_.viewport_->CancelPointPick();window_.viewport_->SetImageViews({});Refresh(window_);
    window_.viewport_->HideToolRoleLabels();window_.viewport_->HideToolPreview();
    image_=QImage();definition_.pngBase64.clear();previewOk_=false;editing_={};
    status_->setText(QStringLiteral("変更を取り消しました。画像ツールはそのままです。画像を選んでください。"));
}
bool V2ImageTool::eventFilter(QObject* object,QEvent* event) {
    if(!isVisible())return false;
    if(event->type()==QEvent::KeyPress){const auto* key=static_cast<QKeyEvent*>(event);auto* widget=qobject_cast<QWidget*>(object);
        if(!widget||widget->window()!=window())return false;
        if(key->key()==Qt::Key_Escape){CancelInput();return true;}
        if(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter){Commit();return true;}}
    if(object!=window_.viewport_)return false;
    if(DragViewport(event))return true;
    if(role_==4&&imageMarks_.size()==1){
        if(event->type()==QEvent::MouseMove){
            const auto* mouse=static_cast<QMouseEvent*>(event);Vector3 pixel,world;
            imageHover_.reset();
            if(mouse->buttons()==Qt::NoButton&&ImagePointAt(mouse->position(),pixel,world))imageHover_=world;
            RefreshFitLine();
        }else if(event->type()==QEvent::Leave){imageHover_.reset();RefreshFitLine();}
    }
    if(event->type()!=QEvent::MouseButtonPress)return false;
    const auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton||mouse->modifiers()!=Qt::NoModifier)return false;
    if(window_.viewport_->PressViewNavigator(mouse->position(),view::AxisArrowModifier::None)!=V2Viewport::ViewPress::None)return true;
    if(role_==0&&image_.isNull()){
        window_.viewport_->SelectAt(mouse->position(),Qt::NoModifier);
        if(!EditSelectedImage())status_->setText(QStringLiteral("貼付済み画像をクリックするか、画像ファイルを選んでください。"));
        return true;
    }
    if(role_==0||role_==2){
        const int previousRole=role_;role_=2;
        if(PickImagePoint(mouse->position())){
            dragging_=true;dragMoved_=false;dragPress_=mouse->position();
            dragOrigin_=definition_.origin;dragNormal_=Cross(definition_.uAxis,definition_.vAxis);
        }else role_=previousRole;
        return true;
    }
    ClickViewport(mouse->position());return true;
}
bool V2ImageTool::DragViewport(QEvent* event) {
    if(!dragging_)return false;
    if(event->type()==QEvent::MouseMove){
        const auto* mouse=static_cast<QMouseEvent*>(event);
        if(!(mouse->buttons()&Qt::LeftButton)){dragging_=false;return false;}
        if((mouse->position()-dragPress_).manhattanLength()>=QApplication::startDragDistance())dragMoved_=true;
        if(dragMoved_)MoveImageDrag(mouse->position());
        return true;
    }
    if(event->type()==QEvent::MouseButtonRelease){
        const auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)return false;
        if(dragMoved_){MoveImageDrag(mouse->position());role_=0;
            if(previewOk_)status_->setText(QStringLiteral("仮置きしました。再ドラッグで調整、Enterで確定、Escで取消。"));}
        dragging_=false;return true;
    }
    return false;
}
void V2ImageTool::MoveImageDrag(const QPointF& pos) {
    // Keep the drag plane fixed: other geometry and grid snaps must not make the image jump.
    const auto point=window_.viewport_->Mapping().UnprojectOntoPlane({pos.x(),pos.y()},dragOrigin_,dragNormal_);
    if(!point){status_->setText(QStringLiteral("この向きでは移動できません。貼付面を正面から見てください。"));return;}
    if(!SetAnchor(*point))return;
    UpdateFields();Preview();
    if(previewOk_)status_->setText(QStringLiteral("画像を移動中。離して仮置き、Enterで確定、Escで取消。"));
}
void V2ImageTool::ClickViewport(const QPointF& pos) {
    if(role_==2||role_==4){PickImagePoint(pos);return;}
    const ScreenPoint screen{pos.x(),pos.y()};const auto ray=window_.viewport_->Mapping().RayThrough(screen);if(!ray)return;
    double nearest=std::numeric_limits<double>::max();app::SelectionRef chosen;
    for(const auto& shape:window_.viewport_->ShapeViews())if(window_.viewport_->EntityShown(shape.entityId))for(const auto& tri:shape.mesh.triangles){
        const auto hit=modeling::RayHitsTriangle(ray->origin,ray->direction,tri);
        if(hit&&*hit<nearest){nearest=*hit;chosen.entityId=shape.entityId;chosen.pickedFaceIndex=tri.faceIndex;}}
    std::optional<Vector3> point;
    if(!chosen.entityId.IsNil()){chosen.kind=app::SelectionElementKind::Face;chosen.hitPoint=ray->origin+ray->direction*nearest;point=chosen.hitPoint;}
    if(role_==1){if(chosen.entityId.IsNil()){status_->setText(QStringLiteral("面の内側をクリックしてください。作業面は『現在の作業面に貼る』で選べます。"));return;}
        if(SetTarget(chosen))Pick(0);return;}
    if(role_!=3&&role_!=5){status_->setText(QStringLiteral("先に右ペインで、配置の基準点かCADの2点を選んでください。"));return;}
    const auto hover=window_.session_->PeekHover(screen);if(hover.snap&&hover.position)point=hover.position;
    if(!point)point=window_.viewport_->Mapping().UnprojectOntoPlane(screen,definition_.origin,Cross(definition_.uAxis,definition_.vAxis));
    if(!point){status_->setText(QStringLiteral("位置を読み取れません。貼付面を正面から見てください。"));return;}
    if(role_==3){if(!SetAnchor(*point))return;role_=0;}
    else {points_.push_back(*point);if(points_.size()==2){
        if(!FitPickedPoints()){points_.clear();return;}role_=0;}}
    UpdateFields();Preview();if(role_!=0)Pick(role_);
}

bool V2ImageTool::ImagePointAt(const QPointF& pos,Vector3& pixel,Vector3& world) const {
    const auto ray=window_.viewport_->Mapping().RayThrough({pos.x(),pos.y()});
    if(!ray||!previewOk_||window_.viewport_->ImageViews().empty())return false;
    const auto& image=window_.viewport_->ImageViews().back();
    double nearest=std::numeric_limits<double>::max();
    for(const auto& triangle:image.triangles){
        const auto hit=modeling::RayHitsTriangle(ray->origin,ray->direction,triangle.mesh);if(!hit||*hit>=nearest)continue;
        const auto point=ray->origin+ray->direction*(*hit);const auto& p=triangle.mesh.points;
        const auto a=p[1]-p[0],b=p[2]-p[0],c=point-p[0];
        const double aa=Dot(a,a),ab=Dot(a,b),bb=Dot(b,b),ca=Dot(c,a),cb=Dot(c,b),det=aa*bb-ab*ab;
        if(std::abs(det)<1e-18)continue;
        const double u=(bb*ca-ab*cb)/det,v=(aa*cb-ab*ca)/det;
        const auto q=triangle.pixels[0]*(1-u-v)+triangle.pixels[1]*u+triangle.pixels[2]*v;
        if(q.x<0||q.y<0||q.x>definition_.pixelWidth||q.y>definition_.pixelHeight)continue;
        nearest=*hit;pixel=q;world=point;
    }
    return nearest!=std::numeric_limits<double>::max();
}
bool V2ImageTool::PickImagePoint(const QPointF& pos) {
    Vector3 pixel,world;
    if(!ImagePointAt(pos,pixel,world)){
        status_->setText(QStringLiteral("3Dビューに表示された画像の内側をクリックしてください。"));return false;}
    if(role_==2){
        // Re-anchor the same mapping; choosing the anchor must not move the image.
        auto delta=(pixel-definition_.anchorPixel)*definition_.mmPerPixel;
        if(definition_.mirrorHorizontal)delta.x=-delta.x;
        const double c=std::cos(definition_.rotationRad),s=std::sin(definition_.rotationRad);
        definition_.anchorUv.x+=(c*delta.x+s*delta.y)/definition_.uvMetric.x;
        definition_.anchorUv.y+=(s*delta.x-c*delta.y)/definition_.uvMetric.y;
        definition_.origin=world;definition_.anchorPixel=pixel;role_=3;
        imageMarks_.clear();UpdateFields();Preview();Pick(3);
    }else {imageMarks_.push_back(world);SetImagePoint(pixel);}
    return true;
}

bool V2ImageTool::FitPickedPoints() {
    const auto before=definition_;
    if(!SetAnchor(points_[0]))return false;
    Vector3 delta;
    if(definition_.followSurface&&face_.Valid()) {
        const auto end=kernel::ImagePointOnSurface(face_,points_[1]);
        if(!end.HasValue()){definition_=before;status_->setText(QString::fromStdString(end.FirstSummaryJa()));return false;}
        delta={(end.Value().uv.x-definition_.anchorUv.x)*definition_.uvMetric.x,
               (end.Value().uv.y-definition_.anchorUv.y)*definition_.uvMetric.y,0};
    } else {
        const auto difference=points_[1]-points_[0];
        delta={Dot(difference,definition_.uAxis),Dot(difference,definition_.vAxis),
               Dot(difference,Normalized(Cross(definition_.uAxis,definition_.vAxis)))};
    }
    const auto fit=modeling::FitImagePoints(pixels_[1]-pixels_[0],delta,
        definition_.mirrorHorizontal,definition_.rotationRad,fitRotate_->isChecked());
    if(!fit.HasValue()){definition_=before;status_->setText(QString::fromStdString(fit.FirstSummaryJa()+" "+fit.FirstDetailsJa()));return false;}
    const double width=fit.Value().mmPerPixel*definition_.pixelWidth;
    if(width<0.00001||width>10000000){definition_=before;status_->setText(QStringLiteral("画像フィット後の幅が設定範囲外です。"));return false;}
    definition_.mmPerPixel=fit.Value().mmPerPixel;definition_.rotationRad=fit.Value().rotationRad;
    definition_.anchorPixel=pixels_[0];imageMarks_.clear();return true;
}
