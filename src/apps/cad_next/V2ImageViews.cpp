#include "V2ImageTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "kachakacha/app/GroupTree.h"
#include "kachakacha/domain/ImageValidation.h"
#include "kachakacha/kernel/OcctImage.h"
#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVariant>
#include <QPointF>
#include "kachakacha/modeling/MeshPick.h"
#include <cmath>
using namespace kachakacha::v2;
bool V2ImageTool::MakeView(const domain::CreateImageDefinition& d,V2ImageView& view,QString& error) {
    if(!domain::ValidImagePlacement(d)){error=QStringLiteral("画像の位置・倍率が正しくありません。");return false;}
    const auto png=QByteArray::fromBase64(QByteArray::fromStdString(d.pngBase64));
    const auto source=QImage::fromData(png,"PNG").convertToFormat(QImage::Format_ARGB32);
    if(source.isNull()||source.width()!=d.pixelWidth||source.height()!=d.pixelHeight){error=QStringLiteral("保存された画像を読み取れません。");return false;}
    const auto mesh=kernel::ImageSurfaceTriangles(d);
    if(!mesh.HasValue()){error=QString::fromStdString(mesh.FirstSummaryJa());return false;}
    view.image.width=source.width();view.image.height=source.height();view.image.opacity=d.opacity;
    view.image.pixels.clear();view.image.pixels.reserve(std::size_t(source.width())*source.height());
    for(int y=0;y<source.height();++y){const auto* row=reinterpret_cast<const std::uint32_t*>(source.constScanLine(y));
        view.image.pixels.insert(view.image.pixels.end(),row,row+source.width());}
    view.triangles=mesh.Value();return true;
}
void V2ImageTool::Refresh(V2MainWindow& window) {
    std::vector<V2ImageView> images;const auto& doc=window.session_->GetDocument();
    const auto key=QString::fromStdString(doc.Snapshot().id.ToString())+QString::number(doc.Revision());
    if(window.viewport_->property("imageDocumentRevision").toString()==key)return;
    window.viewport_->setProperty("imageDocumentRevision",key);
    for(const auto& entity:doc.Snapshot().entities){
        if(entity.kind!=domain::EntityKind::Image||!app::EntityEffectivelyVisible(doc.Snapshot(),entity))continue;
        const auto* feature=doc.FindFeature(entity.createdBy);if(!feature||!feature->enabled)continue;
        const auto* d=std::get_if<domain::CreateImageDefinition>(&feature->definition);if(!d)continue;
        V2ImageView image;image.entityId=entity.id;QString error;
        if(MakeView(*d,image,error))images.push_back(std::move(image));else window.SetStatus(error);
    }
    window.viewport_->SetImageViews(std::move(images));
}

std::vector<app::PickCandidate> V2Viewport::ImageCandidatesAt(const QPointF& position) const {
    std::vector<app::PickCandidate> result;const auto ray=mapping_.RayThrough({position.x(),position.y()});if(!ray)return result;
    for(const auto& image:imageViews_){if(image.entityId.IsNil()||image.image.opacity<=0||!EntityShown(image.entityId))continue;
        for(const auto& triangle:image.triangles){const auto hit=modeling::RayHitsTriangle(ray->origin,ray->direction,triangle.mesh);if(!hit)continue;
            const auto point=ray->origin+ray->direction*(*hit);const auto& p=triangle.mesh.points;
            const auto a=p[1]-p[0],b=p[2]-p[0],c=point-p[0];
            const double aa=geometry::Dot(a,a),ab=geometry::Dot(a,b),bb=geometry::Dot(b,b),ca=geometry::Dot(c,a),cb=geometry::Dot(c,b);
            const double det=aa*bb-ab*ab;if(std::abs(det)<1e-18)continue;
            const double u=(bb*ca-ab*cb)/det,v=(aa*cb-ab*ca)/det;
            const auto pixel=triangle.pixels[0]*(1-u-v)+triangle.pixels[1]*u+triangle.pixels[2]*v;
            if(pixel.x<0||pixel.y<0||pixel.x>=image.image.width||pixel.y>=image.image.height)continue;
            if((image.image.pixels[std::size_t(pixel.y)*image.image.width+std::size_t(pixel.x)]>>24)==0)continue;
            app::PickCandidate picked;picked.entityId=image.entityId;picked.hitPoint=point;result.push_back(picked);break;
        }
    }
    return result;
}
