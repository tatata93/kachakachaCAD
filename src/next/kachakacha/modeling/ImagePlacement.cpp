#include "kachakacha/modeling/ImagePlacement.h"
#include <cmath>
namespace kachakacha::v2::modeling {
using namespace geometry;
Vector3 ImagePixelAt(const domain::CreateImageDefinition& d,const Vector3& p,const Vector3& uv) {
    const auto delta=p-d.origin;
    const double x=d.followSurface ? (uv.x-d.anchorUv.x)*d.uvMetric.x : Dot(delta,d.uAxis);
    const double y=d.followSurface ? (uv.y-d.anchorUv.y)*d.uvMetric.y : Dot(delta,d.vAxis);
    const double c=std::cos(d.rotationRad),s=std::sin(d.rotationRad);
    return {d.anchorPixel.x+(d.mirrorHorizontal?-1:1)*(c*x+s*y)/d.mmPerPixel,d.anchorPixel.y-(-s*x+c*y)/d.mmPerPixel,0};
}
std::vector<ImageTriangle> FlatImageTriangles(const domain::CreateImageDefinition& d) {
    const std::array<Vector3,4> pixels{{{0,0,0},{double(d.pixelWidth),0,0},{double(d.pixelWidth),double(d.pixelHeight),0},{0,double(d.pixelHeight),0}}};
    std::array<Vector3,4> points;const double c=std::cos(d.rotationRad),s=std::sin(d.rotationRad);
    for(std::size_t i=0;i<4;++i){auto p=(pixels[i]-d.anchorPixel)*d.mmPerPixel;if(d.mirrorHorizontal)p.x=-p.x;
        points[i]=d.origin+d.uAxis*(c*p.x+s*p.y)+d.vAxis*(s*p.x-c*p.y);}
    std::vector<ImageTriangle> result;
    for(const auto indices:{std::array<int,3>{0,1,2},std::array<int,3>{0,2,3}}){ImageTriangle triangle;
        for(std::size_t i=0;i<3;++i){triangle.mesh.points[i]=points[indices[i]];triangle.pixels[i]=pixels[indices[i]];}
        triangle.mesh.normal=Normalized(Cross(d.uAxis,d.vAxis));result.push_back(triangle);}
    return result;
}
base::Result<double> ImageScaleFromPoints(const Vector3& a,const Vector3& b,const Vector3& p,const Vector3& q) {
    const double pixels=Distance(a,b),mm=Distance(p,q);
    if(!a.IsFinite()||!b.IsFinite()||!p.IsFinite()||!q.IsFinite()||pixels<1e-6||mm<1e-9)
        return base::Result<double>::Failure(base::MakeError("IMG-001","長さ合わせの2点が重なっています。","画像とCADの両方で離れた2点を選んでください。"));
    return base::Result<double>::Success(mm/pixels);
}
}
