#include "kachakacha/base/TestHarness.h"
#include "kachakacha/domain/ImageValidation.h"
#include "kachakacha/modeling/ImagePlacement.h"
#include "kachakacha/view/SurfaceRaster.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/io/DocumentFile.h"
#include <limits>
using namespace kachakacha::v2;
using namespace test;
namespace {
domain::CreateImageDefinition Image() {
    domain::CreateImageDefinition d;d.pngBase64="embedded png";d.pixelWidth=100;d.pixelHeight=50;
    d.origin={10,20,30};d.anchorPixel={23,17,0};d.mmPerPixel=.2;d.rotationRad=.71;return d;
}
}
KACHA_V2_TEST(image, arbitrary_anchor_rotation_roundtrip) {
    const auto d=Image();Require(domain::ValidImagePlacement(d),"valid placement");
    const auto triangles=modeling::FlatImageTriangles(d);
    for(const auto& t:triangles)for(int i=0;i<3;++i)
        RequireNear(geometry::Distance(modeling::ImagePixelAt(d,t.mesh.points[i]),t.pixels[i]),0,1e-10,"pixel roundtrip");
    RequireNear(geometry::Distance(modeling::ImagePixelAt(d,d.origin),d.anchorPixel),0,1e-10,"anchor fixed");
    const auto scale=modeling::ImageScaleFromPoints({3,4,0},{6,8,0},{10,0,0},{10,30,40});
    Require(scale.HasValue(),"scale computed");RequireNear(scale.Value(),10,1e-10,"3D straight distance");
    Require(!modeling::ImageScaleFromPoints({},{},{},{1,0,0}).HasValue(),"duplicate image points rejected");
    auto bad=d;bad.mmPerPixel=std::numeric_limits<double>::infinity();Require(!domain::ValidImagePlacement(bad),"finite scale");
}
KACHA_V2_TEST(image, uv_and_projection_are_distinct) {
    auto d=Image();d.followSurface=true;d.anchorUv={2,3,0};d.uvMetric={5,9,0};
    const auto uv=modeling::ImagePixelAt(d,{999,888,777},{2,3,0});
    RequireNear(geometry::Distance(uv,d.anchorPixel),0,1e-10,"UV anchor independent of projection");
    d.followSurface=false;Require(geometry::Distance(modeling::ImagePixelAt(d,{999,888,777},{2,3,0}),uv)>1,"projection uses physical axes");
}
KACHA_V2_TEST(image, embedded_image_roundtrip_undo_and_invalid_dimension) {
    base::DeterministicIdGenerator ids{934};document::Document doc(ids.NextTyped<base::IdKind::Document>());
    domain::Feature f;f.id=ids.NextTyped<base::IdKind::Feature>();f.type=domain::FeatureType::CreateImage;auto mirrored=Image();mirrored.mirrorHorizontal=true;f.definition=mirrored;
    domain::Entity e;e.id=ids.NextTyped<base::IdKind::Entity>();e.createdBy=f.id;e.kind=domain::EntityKind::Image;
    f.outputs.push_back({"image",e.id,e.kind});Require(doc.Run(document::AddFeatureCommand(f,{e},"image")).committed,"commit");
    io::DocumentFile file;file.snapshot=doc.Snapshot();const auto archive=io::SaveDocument(file);Require(archive.HasValue(),"save");
    const auto read=io::LoadDocument(archive.Value());Require(read.HasValue(),"load");
    const auto& d=std::get<domain::CreateImageDefinition>(read.Value().snapshot.features.front().definition);
    Require(d.mirrorHorizontal,"mirror survives save");
    RequireEqual(d.pngBase64,Image().pngBase64,"embedded bytes survive");RequireNear(d.mmPerPixel,.2,1e-12,"scale saved");
    Require(doc.Undo()&&doc.Snapshot().entities.empty(),"undo");Require(doc.Redo()&&doc.Snapshot().entities.size()==1,"redo");
    auto bad=Image();bad.pixelWidth=-1;Require(!doc.Run(document::UpdateFeatureDefinitionCommand(f.id,bad,{},"invalid")).committed,"invalid dimensions rejected");
}
KACHA_V2_TEST(image, texture_is_clipped_and_occluded) {
    domain::CreateImageDefinition d;d.pixelWidth=2;d.pixelHeight=2;d.mmPerPixel=5;d.anchorPixel={0,2,0};
    const auto map=geometry::MakeOrthographicMapping({5,5,0},{0,0,-1},{0,1,0},12,80,80);
    view::RasterImage texture{2,2,{0xffff0000,0xffff0000,0xffff0000,0xffff0000},1};
    view::SurfaceRaster raster(80,80);for(const auto& t:modeling::FlatImageTriangles(d))raster.DrawImage(t,map,texture);
    Require(raster.Pixels()[40*80+40]==0xffff0000,"image has original color");Require(raster.Pixels()[0]==0,"outside remains empty");
    d.origin.z=2;for(const auto& t:modeling::FlatImageTriangles(d))raster.Draw(t.mesh,map,{0,0,-1},false,0x00ff00);
    d.origin.z=0;for(const auto& t:modeling::FlatImageTriangles(d))raster.DrawImage(t,map,texture);
    Require((raster.Pixels()[40*80+40]&0xff0000)==0,"front solid hides image");
}
KACHA_V2_TEST(image, transparent_image_has_no_diagonal_double_blend) {
    domain::CreateImageDefinition d;d.pixelWidth=2;d.pixelHeight=2;d.mmPerPixel=5;d.anchorPixel={0,2,0};
    const auto map=geometry::MakeOrthographicMapping({5,5,0},{0,0,-1},{0,1,0},12,80,80);
    view::RasterImage texture{2,2,{0xffff0000,0xffff0000,0xffff0000,0xffff0000},.5};
    view::SurfaceRaster raster(80,80);for(const auto& t:modeling::FlatImageTriangles(d))raster.DrawImage(t,map,texture);
    for(int y=12;y<68;++y)for(int x=12;x<68;++x)
        Require((raster.Pixels()[y*80+x]>>24)==128,"each pixel composited once including diagonal");
}
KACHA_V2_TEST(image, mirrored_anchor_rotation_roundtrip) {
    auto d=Image();d.mirrorHorizontal=true;
    for(const auto& t:modeling::FlatImageTriangles(d))for(int i=0;i<3;++i)
        RequireNear(geometry::Distance(modeling::ImagePixelAt(d,t.mesh.points[i]),t.pixels[i]),0,1e-10,"mirrored pixel roundtrip");
    RequireNear(geometry::Distance(modeling::ImagePixelAt(d,d.origin),d.anchorPixel),0,1e-10,"mirror anchor fixed");
    d.followSurface=true;const auto a=modeling::ImagePixelAt(d,{}, {1,2,0});d.mirrorHorizontal=false;
    const auto b=modeling::ImagePixelAt(d,{}, {1,2,0});
    RequireNear(a.x+b.x,2*d.anchorPixel.x,1e-10,"UV mirror around anchor");
    RequireNear(a.y,b.y,1e-10,"UV vertical unchanged");
}
KACHA_V2_TEST(image, fit_points_rotate_and_dominant_axis) {
    const auto rotated=modeling::FitImagePoints({20,0,0},{0,40,0},false,0,true);
    Require(rotated.HasValue(),"rotated fit");
    auto d=Image();d.mmPerPixel=rotated.Value().mmPerPixel;d.rotationRad=rotated.Value().rotationRad;
    d.anchorPixel={10,5,0};d.origin={4,7,0};
    RequireNear(geometry::Distance(modeling::ImagePixelAt(d,{4,47,0}),{30,5,0}),0,1e-8,"second point matches");
    const auto fixed=modeling::FitImagePoints({20,-5,0},{60,40,0},false,0,false);
    Require(fixed.HasValue(),"dominant X fit");RequireNear(fixed.Value().mmPerPixel,3,1e-9,"fit larger axis");
    const auto vertical=modeling::FitImagePoints({5,-20,0},{40,60,0},false,0,false);
    Require(vertical.HasValue(),"dominant Y fit");RequireNear(vertical.Value().mmPerPixel,3,1e-9,"fit larger axis");
    Require(!modeling::FitImagePoints({20,0,0},{0,40,0},false,0,false).HasValue(),"cannot fit zero X without rotation");
    Require(!modeling::FitImagePoints({20,0,0},{40,0,1},false,0,true).HasValue(),"out of plane rejected");
    const auto mirrored=modeling::FitImagePoints({20,0,0},{0,40,0},true,0,true);
    Require(mirrored.HasValue(),"mirrored fit");d.mirrorHorizontal=true;d.rotationRad=mirrored.Value().rotationRad;
    RequireNear(geometry::Distance(modeling::ImagePixelAt(d,{4,47,0}),{30,5,0}),0,1e-8,"mirrored second point");
}
KACHA_V2_TEST_MAIN("image_placement_tests")
