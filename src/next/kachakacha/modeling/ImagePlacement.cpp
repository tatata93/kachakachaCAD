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
base::Result<ImageFit> FitImagePoints(const Vector3& pixels, const Vector3& target,
    bool mirror, double rotation, bool rotate)
{
    using Result = base::Result<ImageFit>;
    const Vector3 source{mirror ? -pixels.x : pixels.x, -pixels.y, 0};
    if (!pixels.IsFinite() || !target.IsFinite() || !std::isfinite(rotation)
        || source.Length() < 1e-6 || target.Length() < 1e-9 || std::abs(target.z) > 1e-6) {
        return Result::Failure(base::MakeError("IMG-002", "画像フィットの2点が不正です。",
            "同じ貼付平面上で、離れた2点を指定してください。"));
    }
    double scale;
    if (rotate) {
        scale = std::hypot(target.x, target.y) / source.Length();
        rotation = std::atan2(target.y, target.x) - std::atan2(source.y, source.x);
    } else {
        const double c = std::cos(rotation), s = std::sin(rotation);
        const Vector3 direction{c*source.x-s*source.y, s*source.x+c*source.y, 0};
        scale = std::abs(direction.x) >= std::abs(direction.y)
            ? target.x / direction.x : target.y / direction.y;
    }
    if (!std::isfinite(scale) || scale <= 1e-9) {
        return Result::Failure(base::MakeError("IMG-003", "指定方向では画像を合わせられません。",
            "配置先の2点の順を変えるか「回転する」を有効にしてください。"));
    }
    return Result::Success({scale, rotation});
}

}
