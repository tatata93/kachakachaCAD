#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "kachakacha/view/SurfaceRaster.h"
#include <QApplication>
#include <QColor>
#include <QImage>
#include <QDir>
#include <QString>
#include <algorithm>
#include <array>
#include <cmath>

namespace kachakacha::v2::selftest {
namespace {
V2ImageView Image(base::EntityId id,double z,double opacity,std::uint32_t color) {
    V2ImageView image;image.entityId=id;image.image={2,2,{color,color,color,color},opacity};
    const std::array<geometry::Vector3,4> p{{{-32,-26,z},{32,-26,z},{32,26,z},{-32,26,z}}};
    const std::array<geometry::Vector3,4> uv{{{0,0,0},{2,0,0},{2,2,0},{0,2,0}}};
    for(const auto indices:{std::array<int,3>{0,1,2},std::array<int,3>{0,2,3}}) {
        modeling::ImageTriangle t;
        for(int i=0;i<3;++i){t.mesh.points[i]=p[indices[i]];t.pixels[i]=uv[indices[i]];}
        image.triangles.push_back(t);
    }
    return image;
}
QImage Frame(V2Viewport& view) {
    QImage image(view.size(),QImage::Format_ARGB32);image.fill(Qt::transparent);view.render(&image);return image;
}
}

bool GpuImageDepthAndReuse(V2MainWindow& window) {
    window.RunCommand("file.new");window.resize(1500,950);QApplication::processEvents();
    auto& view=window.Viewport();view.SetViewDirection(ViewDirection::Top);view.SetViewCenter({});view.SetVisibleWidthMm(100);
    auto display=view.DisplaySettingsNow();display.gridVisible=false;display.workPlaneVisible=false;view.SetDisplaySettings(display);
    base::DeterministicIdGenerator ids(982000);
    const auto shapeId=ids.NextTyped<base::IdKind::Entity>();
    const auto imageId=ids.NextTyped<base::IdKind::Entity>(),secondId=ids.NextTyped<base::IdKind::Entity>();
    modeling::MeshTriangle triangle;triangle.points={geometry::Vector3{-20,-20,0},{20,-20,0},{0,22,0}};triangle.normal={0,0,1};
    modeling::ShapeMesh mesh;mesh.triangles={triangle};modeling::RefreshBounds(mesh);
    view.SetShapeViews({{shapeId,mesh,true}});
    auto back=Image(imageId,-2,1,0xff25d660u),front=Image(secondId,2,.5,0x80e64228u);
    front.image.pixels={0x80e64228u,0xff2040d0u,0x40ffe020u,0xff30e0a0u};
    view.SetImageViews({back,front});
    const auto picture=Frame(view);const auto uploads=view.ImageTextureUploads();const auto counts=view.InteractionCounts();
    const bool gpu=QString::fromUtf8(view.RenderBackend())!="CPU";
    // CPU compositor is an independent reference for image alpha and CAD depth.
    view::SurfaceRaster reference(view.width(),view.height());
    reference.Draw(triangle,view.Mapping(),view::ForwardOf(view.Orientation()),false,0x91bed9);
    for(const auto* image:{&back,&front})for(const auto& t:image->triangles)reference.DrawImage(t,view.Mapping(),image->image);
    for(const auto point:{geometry::Vector3{-10,-12,0},geometry::Vector3{12,-12,0},geometry::Vector3{-26,12,0}}) {
        const auto at=view.Mapping().Project(point);if(!at)return false;
        const int x=int(at->x),y=int(at->y);const auto expected=QColor::fromRgba(reference.Pixels()[std::size_t(y)*view.width()+x]);
        // All test pixels have an opaque lower layer, avoiding GUI background assumptions.
        const auto actual=picture.pixelColor(x,y);
        if(!Explain("textured image alpha/depth agrees with CPU within raster rounding",
            std::abs(expected.red()-actual.red())+std::abs(expected.green()-actual.green())+std::abs(expected.blue()-actual.blue())<=12))return false;
    }
    for(int i=0;i<5;++i){view.PanByPixels(2,1);Frame(view);}
    if(gpu&&!Explain("camera movement reuses image textures and avoids CPU compositing",
        view.ImageTextureUploads()==uploads&&view.InteractionCounts().rasterizations==counts.rasterizations))return false;
    app::SelectionSet onlyShape;onlyShape.entityIds={shapeId};view.SetSelection(onlyShape);
    display.selectionOnly=true;view.SetDisplaySettings(display);const auto hidden=Frame(view);
    display.selectionOnly=false;view.SetDisplaySettings(display);
    front.image.pixels.assign(4,0xff1840e0u);front.image.opacity=1;view.SetImageViews({back,front});
    if(!Explain("visibility and replaced image pixels invalidate image composition",Frame(view)!=hidden))return false;
    // A warped quadrilateral uses the original two noncoplanar triangles and UVs.
    front.triangles[0].mesh.points[2].z=4;front.triangles[1].mesh.points[1].z=4;
    view.SetShapeViews({});view.SetSelection({});view.SetImageViews({front});view.OrbitByPixels(45,24);
    const auto curved=Frame(view);
    const auto center=(front.triangles[0].mesh.points[0]+front.triangles[0].mesh.points[1]+front.triangles[0].mesh.points[2])*(1./3);
    const auto checkBlue=[&](const QImage& image) {
        const auto at=view.Mapping().Project(center);if(!at)return false;
        const auto color=image.pixelColor(int(at->x),int(at->y));
        return std::abs(color.red()-24)+std::abs(color.green()-64)+std::abs(color.blue()-224)<=8;
    };
    if(!Explain("image-only warped triangle retains its inner pixels",checkBlue(curved)))return false;
    window.resize(1570,980);QApplication::processEvents();
    if(!Explain("resized image-only triangle retains its inner pixels",checkBlue(Frame(view))))return false;
    auto invalid=back;invalid.image.width=0;view.SetImageViews({front,invalid});
    if(!Explain("failed texture upload retains a complete software fallback",checkBlue(Frame(view))))return false;
    view.SetImageViews({front});
    if(!Explain("valid textures recover after a failed upload",checkBlue(Frame(view))))return false;
    const auto proof=qEnvironmentVariable("KACHACAD_GRID_PROOF_DIR");
    if(!proof.isEmpty()){QDir(proof).mkpath(".");picture.save(QDir(proof).filePath("image-depth.png"));window.grab().save(QDir(proof).filePath("image-warped.png"));}
    view.SetImageViews({});view.SetShapeViews({});return true;
}
}
